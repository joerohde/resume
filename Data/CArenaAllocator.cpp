// This is a class for a a specific pattern of memory allocation.
// When an environment needs:
// - dynamic allocation of many objects
// - of the same or varying sizes
// - the max size is known
// - all the objects of a group can be released at once.
//
// Create an arena for the life of the 'pool'.  Allocations
// are time O(1).  Free is O(1).  Finally free is basically
// the time it takes to call free on each large chunk.
//
// This pattern is common in parsers where a parse tree per
// 'function' is created and the whole tree can be dumped when
// done with it.

#include "PreCompile.h"

#include "os.h"
#include "CArenaAllocator.h"

#if defined(_DEBUG) || defined(_WANT_STATS)
#define _MARKER_SIZE 4
#endif

#ifdef _DEBUG

static const BYTE _HEAD_MARKER[_MARKER_SIZE] = { 0xDE, 0xAD, 0xBE, 0xEF };
static const BYTE _TAIL_MARKER[_MARKER_SIZE] = { 0xFE, 0xEB, 0xDA, 0xED };

// Overlay on the front of an arbitrary allocation
struct CArenaAllocHeader
{
	// so we can find the tail marker and the next allocation
	unsigned short	nAllocationSize; // Not including pre/post buffers
	unsigned short	nDeleteCount;
	BYTE	marker[_MARKER_SIZE];

	BYTE *Buffer() const { return ((BYTE*)this + ALIGN_TO_POINTER(sizeof(CArenaAllocHeader))); }
	BYTE *TailMarker() const { return Buffer() + this->nAllocationSize; }
	CArenaAllocHeader* Next() const { return (CArenaAllocHeader*)(Buffer() + ALIGN_TO_POINTER(this->nAllocationSize + _MARKER_SIZE)); }
};

static void AddAllocMarkers(CArenaAllocHeader *pHeader);
static void VerifyPage(const CArenaPageHeader *pHeader);
static void VerifyDeadAllocation(const CArenaAllocHeader *pHeader);

#endif

// Overlay on the front of each page.
struct CArenaPageHeader
{
	CArenaPageHeader	*pNextPage;
	BYTE *Buffer() const { return ((BYTE*)this + ALIGN_TO_POINTER(sizeof(CArenaPageHeader))); }

#ifdef _DEBUG
	BYTE		*highWaterMark;
	size_t		nPageSize; // does include any Pre/Post Buffers!
	BYTE		marker[_MARKER_SIZE];

	BYTE *TailMarker() const { return (BYTE*)this + this->nPageSize - _MARKER_SIZE; }
#endif
};

CArenaAllocator::CArenaAllocator() :
	 m_pCurrPage(0), m_pCurrentFree(0), m_pBarrier(0)
{
#ifdef _WANT_STATS
	m_totalBytesAllocated = 0;
	m_totalBytesRequested = 0;
	m_pagesAllocated = 0;
	m_numRequests = 0;
#endif

#ifndef _DEBUG
	// Should be our /only/ overhead in release mode.
	StaticAssert(sizeof(CArenaPageHeader) == sizeof(BYTE*));
#endif
}

CArenaAllocator::~CArenaAllocator()
{
	FreeAllPages();
}

void CArenaAllocator::MakeFirstPage()
{
	// An interpreter is often instantiated just to evaluate variables being passed 
	// across scripts.  This often ends up in a parse tree with 0-ish bytes.  
	// In addition, a new parse/binary tree is built for 'eval' statements.  
	// These tend to go anywhere from 16 to 256 bytes (tending to the smaller) 
	// unless the eval was machine generated.  As such, defer creating the 
	// first page until there is a request for memory - this optimizes for the
	// running of scripts that don't take parms.  Then make the  first page much 
	// smaller than normal to optimize for the 'some parameters to a script' 
	// case, and the 'eval' case.
	const size_t kFirstPageSize = 192;
	m_pCurrPage = new BYTE[ kFirstPageSize ];
	if (m_pCurrPage == NULL)
	{
		return;
	}
	m_pBarrier = m_pCurrPage + kFirstPageSize;

	CArenaPageHeader *pHeader = (CArenaPageHeader *)m_pCurrPage;
	m_pCurrentFree = pHeader->Buffer();
	pHeader->pNextPage = NULL;

	DBG_ONLY(pHeader->nPageSize = kFirstPageSize);
	DBG_ONLY(AddPageMarkers(pHeader));

	// Don't count overhead. We want to know how many bytes of page get left over.
	STAT_ONLY(m_totalBytesAllocated = kFirstPageSize - sizeof(CArenaPageHeader) - _MARKER_SIZE);
	STAT_ONLY(m_pagesAllocated = 1);
}

void CArenaAllocator::FreeAllPages()
{
#ifdef _WANT_ARENA_STATS
	SD2("   Arena: # of Requests:   %d\n", m_numRequests);
	SD2("   Arena: Bytes Requested: %d\n", m_totalBytesRequested);
	SD2("   Arena: Bytes Allocated: %d\n", m_totalBytesAllocated);
	SD2("   Arena: Pages Allocated: %d\n", m_pagesAllocated);
#endif
	CArenaPageHeader *pPage = (CArenaPageHeader *)m_pCurrPage;
	while (pPage)
	{
		DBG_ONLY( VerifyPage( pPage ) );

		CArenaPageHeader *pNextPage = pPage->pNextPage;
		delete [] pPage;

		pPage = pNextPage;
	}
}

void *CArenaAllocator::GrowPagesAndAllocate(size_t nMinSize) // return value can be returned to user
{
	const size_t kIdealPageSize = 4096; // paramter to constructor? DAL_Variable? Both?

	Assert(m_pCurrentFree + nMinSize > m_pBarrier);

	// Get the 1st page?
	if (m_pCurrentFree == NULL)
	{
		MakeFirstPage();
		if (m_pCurrentFree == NULL)
		{
			return NULL;
		}
		// Didn't have a page. Now we do, see if the
		// request actually fit
		if (m_pCurrentFree + nMinSize <= m_pBarrier)
		{
			void *pMem = m_pCurrentFree;
			m_pCurrentFree += ALIGN_TO_POINTER(nMinSize);
			return pMem;
		}
	}

	size_t nBytesToAllocate = kIdealPageSize;
	bool bOrphanBlock = false;
	// min page size is size of block requested, size of header, and size of tail marker
	size_t nMinPageSize = nMinSize + ALIGN_TO_POINTER(sizeof(CArenaPageHeader));
	DBG_ONLY( nMinPageSize += _MARKER_SIZE ); // tail marker in debug

	if (nMinPageSize > (kIdealPageSize>>1) )
	{
		// Need to handle the case where the requested block is larger than
		// our ideal block size.  In addition, if it's bigger than 1/2 the 
		// ideal size and didn't fit, let's also give it a dedicated block.

		// Insert a 'finished' page to the page list. 
		// This lets the next allocation try to use the remaining space
		// in the current page.

		bOrphanBlock = true;
		nBytesToAllocate = nMinPageSize;
	}

	// Don't count overhead. We want to know how many bytes of page get left over.
	STAT_ONLY(m_totalBytesAllocated += (nBytesToAllocate - sizeof(CArenaPageHeader) - _MARKER_SIZE));
	STAT_ONLY(m_pagesAllocated++);

	Assert(m_pCurrPage != NULL);

	CArenaPageHeader *pNewFirstPage = (CArenaPageHeader*)new char[ nBytesToAllocate ];
	if (pNewFirstPage == NULL)
	{
		return NULL;
	}

	pNewFirstPage->pNextPage = (CArenaPageHeader *)m_pCurrPage;
	DBG_ONLY(pNewFirstPage->nPageSize = nBytesToAllocate);

	m_pCurrPage = (BYTE*)pNewFirstPage;
	BYTE* pMem = pNewFirstPage->Buffer();

	// Did we actually make a new arena page,
	// or just a 1-off orphan?
	if (!bOrphanBlock)
	{
		m_pBarrier = (BYTE*)pNewFirstPage + nBytesToAllocate;
		m_pCurrentFree = pMem + ALIGN_TO_POINTER(nMinSize);
	}

	DBG_ONLY(AddPageMarkers(pNewFirstPage));

	return pMem;
}

#ifdef _DEBUG
void *CArenaAllocator::DbgAllocate(size_t nBytesRequested)
{
	m_totalBytesRequested += nBytesRequested;
	m_numRequests++;

	// Make room for guard markers and info
	size_t nBytes = nBytesRequested + ALIGN_TO_POINTER((sizeof(CArenaAllocHeader)) + _MARKER_SIZE);

	BYTE *pMem;
	// m_pBarrier is the 1st illegal address
	if (m_pCurrentFree + nBytes <= m_pBarrier)
	{
		pMem = m_pCurrentFree;
		m_pCurrentFree += ALIGN_TO_POINTER(nBytes);
	}
	else
	{
		pMem = (BYTE*)GrowPagesAndAllocate(nBytes); // GrowPages responsible for fixing up state
	}

	CArenaAllocHeader *pHeader = (CArenaAllocHeader*) pMem;
	Assert(nBytesRequested <= 0xFFFF); // nAllocationSize is a short
	pHeader->nAllocationSize = CAST_TO_USHORT( MIN( nBytesRequested, 0xFFFFu )); // Does not include header/marker space!!!
	AddAllocMarkers(pHeader);

	CArenaPageHeader *pPage = (CArenaPageHeader*)m_pCurrPage;
	pPage->highWaterMark = m_pCurrentFree;

	return pHeader->Buffer();
}

// Verify and dirty up that space
void CArenaAllocator::DbgDeleteHelper(void *ptr)
{
	BYTE *pbyteHeader = (BYTE*)ptr - ALIGN_TO_POINTER(sizeof(CArenaAllocHeader));
	CArenaAllocHeader *pHeader = (CArenaAllocHeader*)pbyteHeader;
	pHeader->nDeleteCount++;
	VerifyDeadAllocation(pHeader);

	// Crush the data in case someone tries to read from it after destruction
	memset(ptr, 0xCA, pHeader->nAllocationSize);
}

static void AddAllocMarkers(CArenaAllocHeader *pHeader)
{
	// While we take pretty good care to make sure we handle align correctly
	// in general, we would need to code the 'underflow' stuff more carefully
	// if we don't have natural alignmenment on the header. If we don't
	// naturally align after the header, the current code would leave a 'gap'
	// that we would not detect an underflow over-write in.
	StaticAssert(sizeof(CArenaAllocHeader) == ALIGN_TO_POINTER(sizeof(CArenaAllocHeader)));

	pHeader->nDeleteCount = 0;
	memcpy(pHeader->marker, _HEAD_MARKER, _MARKER_SIZE);
	if (pHeader->nAllocationSize != 0xFFFF)
	{
		BYTE *pTailMarker = pHeader->TailMarker();
		memcpy(pTailMarker, _TAIL_MARKER, _MARKER_SIZE);
	}
}

void CArenaAllocator::AddPageMarkers(CArenaPageHeader *pHeader)
{
	// While we take pretty good care to make sure we handle align correctly
	// in general, we would need to code the 'underflow' stuff more carefully
	// if we don't have natural alignmenment on the header. If we don't
	// naturally align after the header, the current code would leave a 'gap'
	// that we would not detect an underflow over-write in.
	StaticAssert(sizeof(CArenaPageHeader) == ALIGN_TO_POINTER(sizeof(CArenaPageHeader)));

	// Flip head/tail markers at the page level.
	m_pBarrier -= _MARKER_SIZE;
	Assert(m_pBarrier > m_pCurrentFree); // if this fails our pages are too small
	pHeader->highWaterMark = pHeader->Buffer();
	memcpy(pHeader->marker, _TAIL_MARKER, _MARKER_SIZE);
	memcpy(pHeader->TailMarker(), _HEAD_MARKER, _MARKER_SIZE);
}

static void VerifyPage(const CArenaPageHeader *pPage)
{
	// HEAD/TAIL markers are used other way around for 'page' level marking.
	Assert(memcmp(pPage->marker, _TAIL_MARKER, _MARKER_SIZE) == 0);
	BYTE *pEndPageMarker = pPage->TailMarker();
	Assert(memcmp(pEndPageMarker, _HEAD_MARKER, _MARKER_SIZE) == 0);

	// Look for any non-destructor called objects in the page.
	BYTE *pAlloc = pPage->Buffer();
	while (pAlloc < pPage->highWaterMark)
	{
		CArenaAllocHeader *pAllocHeader = (CArenaAllocHeader *)pAlloc;
		VerifyDeadAllocation(pAllocHeader);
		pAlloc = (BYTE*)pAllocHeader->Next();
	}

}

static void VerifyDeadAllocation(const CArenaAllocHeader *pHeader)
{
	// If any of these asserts fire on a reproducable pHeader address,
	// set a conditional breakpoint on the return statement of 
	// DbgAllocate() for pHeader and check the call stack.
	Assert(pHeader->nDeleteCount != 0); // Never deleted!
	Assert(pHeader->nDeleteCount == 1); // Multi-Delete calls!
	Assert(memcmp(pHeader->marker, _HEAD_MARKER, _MARKER_SIZE) == 0); // Underrun!

	if (pHeader->nAllocationSize != 0xFFFF) // request was too big
	{
		BYTE *pTailMarker = pHeader->TailMarker();
		// Overrun!
		Assert(memcmp(pTailMarker, _TAIL_MARKER, _MARKER_SIZE) == 0);
	}
}

#endif

