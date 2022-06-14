#ifndef _DEF_CARENAALLOCATOR_H_
#define _DEF_CARENAALLOCATOR_H_

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

//#define _DONT_USE_ARENA
//#define _WANT_ARENA_STATS // enable to see arena stats, also set _WANT_STATS on the project if Release mode.

struct CArenaPageHeader;

class CArenaAllocator
{
	NON_COPYABLE(CArenaAllocator)
public:
	CArenaAllocator();
	~CArenaAllocator();

	inline
	void *Allocate(size_t nBytes)
	{
#ifdef _DEBUG
		// Allocation under debug is too much work to try to ifdef it
		// into the inline-able Release path.
		return DbgAllocate(nBytes);
#else
		STAT_ONLY(m_totalBytesRequested += nBytes);
		STAT_ONLY(m_numRequests++);

		void *pMem;
		// m_pBarrier is the 1st illegal address
		if (m_pCurrentFree + nBytes <= m_pBarrier)
		{
			pMem = m_pCurrentFree;
			m_pCurrentFree += ALIGN_TO_POINTER(nBytes);
		}
		else
		{
			pMem = GrowPagesAndAllocate(nBytes); // GrowPages responsible for fixing up state
		}
		return pMem;
#endif
	}

private:
	void FreeAllPages();
	void *GrowPagesAndAllocate(size_t nMinSize); // return value can be returned to user
	void MakeFirstPage();

	BYTE	*m_pCurrPage;		// Where to start freeing pages from.
	BYTE	*m_pCurrentFree;	// Where to allocate next block if it fits. 
	BYTE	*m_pBarrier;		// Ceiling to decide if it fits.

#ifdef _DEBUG
public:
	static void DbgDeleteHelper(void *ptr);
private:
	void *DbgAllocate(size_t nBytes);
	void AddPageMarkers(CArenaPageHeader *pHeader);
#endif
#if defined(_DEBUG) || defined(_WANT_STATS)
	size_t m_totalBytesAllocated;
	size_t m_totalBytesRequested;
	size_t m_pagesAllocated;
	size_t m_numRequests;
#endif
};


// @TODO:JTR Do we need to worry about array allocator.
#ifdef _DONT_USE_ARENA
	#define _ARENA_NEW_HELPER(pArena,nBytes)	inline void *operator new(size_t nBytes, CArenaAllocator *pArena) { return new char[ nBytes ]; }
	#define _ARENA_DELETE_HELPER(ptr)			inline void operator delete(void *ptr) { delete [] ptr; }
#else
	#define _ARENA_NEW_HELPER(pArena,nBytes)	inline void *operator new(size_t nBytes, CArenaAllocator *pArena) { return pArena->Allocate(nBytes); }
  #ifdef _DEBUG
	#define _ARENA_DELETE_HELPER(ptr)			inline void operator delete(void *ptr) { CArenaAllocator::DbgDeleteHelper(ptr); }
  #else
	#define _ARENA_DELETE_HELPER(ptr)			inline void operator delete(void *ptr) { }
  #endif
#endif

#endif // _DEF_CARENAALLOCATOR_H_
