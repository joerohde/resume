#pragma once

// To see the interesting parts of this code, prefer looking at (in order of interest):
// - SelectRandom
// - TraverseRandom
// - RotateLeft/Right
// - Insert
// - AssertValid
// Bits having to do with the 'random' stuff are commented //RND

// This is a simple red black tree built from Sedgwick's reference description
// and code snippets as well as his 2-3-4 tree description.  It is non-optimized
// since this code is about showing the mixing in of the weighted random selection
// algorithm.  Weighting of 0 remove the item from consideration for selection.

// Things that should be done to make this more 'real'.
//
// - Get rid of the recursion; and subsequently determine if it's better
//   to have keep stacks around, or just add parent pointers to the Node type.
//
// - Support custom memory allocators, or at least have a decent allocation
//   story if this is to be heavily used.
//
// - Make it STL-Like
//
// - Support STL Iterators
//
// - Make it thread safe
//
// - Better error handling.  This one just has asserts and return values.
//   Often exception handling really is a better choice (especially on
//   x64/Itanic architectures).
//
// - Add constraints on TData to force comparability and copy constructability
//   and a Weight() method or interface.
//
// - To make things more readable null checks are typically done on input
//   parameters of recursive functions.  Nice for readability, but an extra
//   level of recursion without a /really/ good optimizer. <shrug>
//


template <typename TData>
class Tree
{
// public types
	typedef void (TData::* TraverseCallBack)(void);
	
// Interface
public:
	Tree(void);
	~Tree(void);

	void Add(const TData &data);

	// Don't have time to re-implement this exercise in tedium.  It barely impacts
	// the weighted random algorithm any more than insert, except the book-keeping
	// during the normal 'bst delete' at the start.

	// bool Remove(const TData &data); // return true if removed
	// size_t Count() const; // eh, easy to implement later.

	void TraverseInOrder(TraverseCallBack callback) const;

	//RND Select a random item, by weighted preference.
	bool SelectRandom(TraverseCallBack callback, bool bAllowRepeat = false) const;

	//RND visit each node exactly once in the weighted random order.
	void TraverseRandom(TraverseCallBack callback) const;

	//RND set all the weights back to original values
	void ResetWeights();

	// Verify RedBlack properties, and calidate integrity of weighted values
	void AssertValid() const;

// Internal data types
private:
	typedef struct Node
	{
		Node	*pLeft;
		Node	*pRight;
		size_t	nWeight;
		unsigned
		__int64	nSummedWeight;
		TData	data;
		bool	bRed;

		// a theoretical 2-3-4 'node'.
		Node(const TData &srcData) : data(srcData), pLeft(NULL), pRight(NULL), bRed(true), nWeight(srcData.Weight())
		{
			// initialize this explicitely so as to never confuse anyone who isn't clear
			// about constructor initialization order being dependant on declaration.
			// (ie: insulate ourselves from someone re-ordering the struct, while avoiding
			// calling srcData.Weight() twice since we don't know if that call is expensive or not).
			nSummedWeight = nWeight;
		}

	} *PNODE;

// Internal Methods
private:
	// return value is the weight on the new data
	static size_t Insert(PNODE &pCurrent, const TData &data, bool bRed);

	// callback will be called when selection hit
	static size_t SelectRandom(const PNODE pNode, unsigned __int64 nRandom, TraverseCallBack callback, bool bAllowRepeat);

	// destructor helper
	static void DeleteFromRoot(PNODE pNode);

	// Helpers to make null checking less intrusive
	static bool IsRed(const PNODE pn) { return pn ? pn->bRed : false; }
	static unsigned __int64 GetSummedWeight(const PNODE pn) { return pn ? pn->nSummedWeight : 0; }

	// Universal
	static void RotateLeft(PNODE &pNode);
	static void RotateRight(PNODE &pNode);

	// recursion helper
	static void Traverse(PNODE pNode, TraverseCallBack callback);

	static unsigned __int64 ResetWeight(PNODE pNode);

	// Verify RedBlack properties, and validate integrity of weighted values
	static unsigned __int64 AssertValid(const PNODE pNode, int nBlackCountSeen, int *pnBlackTotal);

// Internal data
private:
	PNODE	m_pRoot;
};

template<typename TData>
Tree<TData>::Tree()
: m_pRoot(NULL)
{

}

template<typename TData>
Tree<TData>::~Tree()
{
	DeleteFromRoot(m_pRoot);
}

template<typename TData>
void Tree<TData>::DeleteFromRoot(PNODE pNode)
{
	if (pNode)
	{
		if (pNode->pLeft) DeleteFromRoot(pNode->pLeft);
		if (pNode->pRight) DeleteFromRoot(pNode->pRight);

		delete pNode;
	}
}

template<typename TData>
void Tree<TData>::Add(const TData &data)
{
	// Pretty much right from Sedgewick
	Insert(m_pRoot, data, false);
	m_pRoot->bRed = false;
}

template<typename TData>
size_t Tree<TData>::Insert(PNODE &pCurrent, const TData &data, bool bFlip)
{
	if (NULL == pCurrent)
	{	// Root not set
		pCurrent = new Node(data);
		return pCurrent->nWeight;
	}

	if (data == pCurrent->data)
	{ // match. Replace it since equality is not the same as identity, assume we want the 'freshest' item
		pCurrent->data = data;
		pCurrent->nWeight = data.Weight();
		pCurrent->nSummedWeight = pCurrent->nWeight + GetSummedWeight(pCurrent->pLeft) + GetSummedWeight(pCurrent->pRight);
		return pCurrent->nWeight;
	}

	size_t nWeight;

	// 4 node, split it now, fix it on the way back up.
	if (IsRed(pCurrent->pLeft) && (IsRed(pCurrent->pRight)))
	{
		pCurrent->bRed = true;
		pCurrent->pLeft->bRed = false;
		pCurrent->pRight->bRed = false;
	}

	if (data < pCurrent->data)
	{ // go left
		nWeight = Insert(pCurrent->pLeft, data, false);
		pCurrent->nSummedWeight += nWeight;

		if (IsRed(pCurrent) && IsRed(pCurrent->pLeft) && bFlip)
		{
			RotateRight(pCurrent);
		}

		// pCurrent->pLeft->pLeft won't deref null because of short circuit eval on the left side
		if (IsRed(pCurrent->pLeft) && IsRed(pCurrent->pLeft->pLeft))
		{
			RotateRight(pCurrent);
			pCurrent->bRed = false;
			pCurrent->pRight->bRed = true;
		}
	}
	else
	{ // go right
		nWeight = Insert(pCurrent->pRight, data, true);
		pCurrent->nSummedWeight += nWeight;

		if (IsRed(pCurrent) && IsRed(pCurrent->pRight) && !bFlip)
		{
			RotateLeft(pCurrent);
		}
		if (IsRed(pCurrent->pRight) && IsRed(pCurrent->pRight->pRight))
		{
			RotateLeft(pCurrent);
			pCurrent->bRed = false;
			pCurrent->pLeft->bRed = true;
		}
	}

	return nWeight;
}

//RND
template<typename TData>
void Tree<TData>::ResetWeights()
{	
	ResetWeight(m_pRoot);
}

template<typename TData>
unsigned __int64 Tree<TData>::ResetWeight(PNODE pNode)
{
	if (NULL == pNode)
	{
		return 0;
	}

	pNode->nWeight = pNode->data.Weight();
	pNode->nSummedWeight = pNode->nWeight;

	pNode->nSummedWeight += ResetWeight(pNode->pLeft);
	pNode->nSummedWeight += ResetWeight(pNode->pRight);

	return pNode->nSummedWeight;
}

//RND Select a random item, by weighted preference.
template<typename TData>
bool Tree<TData>::SelectRandom(TraverseCallBack callback, bool bAllowRepeat) const
{
	if ((NULL == m_pRoot) || (m_pRoot->nSummedWeight == 0))
	{
		return false;
	}

	__int64 nRandom = rand();
	nRandom = (nRandom << 32) | rand();
	nRandom %= m_pRoot->nSummedWeight;

	SelectRandom(m_pRoot, nRandom, callback, bAllowRepeat);

	return true;
}

//RND: The selector
template<typename TData>
size_t Tree<TData>::SelectRandom(const PNODE pNode, unsigned __int64 nRandom, TraverseCallBack callback, bool bAllowRepeat)
{
	ASSERT(pNode);

	size_t nWeight = pNode->nWeight;
	if (nRandom < nWeight)
	{
		if (!bAllowRepeat) pNode->nWeight = 0;

		(pNode->data.*callback)();
	}
	else
	{
		nRandom -= nWeight;

		if (nRandom < GetSummedWeight(pNode->pLeft))
		{
			nWeight = SelectRandom(pNode->pLeft, nRandom, callback, bAllowRepeat);
		}
		else
		{
			nRandom -= GetSummedWeight(pNode->pLeft);
			nWeight = SelectRandom(pNode->pRight, nRandom, callback, bAllowRepeat);
		}
	}

	if (!bAllowRepeat)
	{
		pNode->nSummedWeight -= nWeight;
	}

	return nWeight;
}

//RND visit each node exactly once in the weighted random order.
template<typename TData>
void Tree<TData>::TraverseRandom(TraverseCallBack callback) const
{
	while (SelectRandom(callback, false))
		;
}

template<typename TData>
void Tree<TData>::TraverseInOrder(TraverseCallBack callback) const
{
	Traverse(m_pRoot, callback);
}

template<typename TData>
void Tree<TData>::Traverse(PNODE pNode, TraverseCallBack callback)
{
	if (NULL == pNode) return;

	Traverse(pNode->pLeft, callback);
	
	(pNode->data.*callback)();

	Traverse(pNode->pRight, callback);
}

//RND: As the items rotate, the weighted sums must be kept in sync
template<typename TData>
void Tree<TData>::RotateLeft(PNODE &pNode)
{
	PNODE pRight = pNode->pRight;

	// Update the counts first
	// The point of the rotate is that the right child exists and
	// replaces the current node. So pick up that weight.
	pRight->nSummedWeight = pNode->nSummedWeight;

	// The current node will 'lose' the weight of right's right tree and right itself.
	pNode->nSummedWeight -= (GetSummedWeight(pRight->pRight) + pRight->nWeight);

	pNode->pRight = pRight->pLeft;
	pRight->pLeft = pNode;
	pNode = pRight;
}

//RND: As the items rotate, the weighted sums must be kept in sync
template<typename TData>
void Tree<TData>::RotateRight(PNODE &pNode)
{
	PNODE pLeft = pNode->pLeft;

	// Update the counts first
	// The point of the rotate is that the left child exists and
	// replaces the current node. So pick up that weight.
	pLeft->nSummedWeight = pNode->nSummedWeight;

	// The current node will 'lose' the weight of left's left tree and left iself.
	pNode->nSummedWeight -= (GetSummedWeight(pLeft->pLeft) + pLeft->nWeight);

	pNode->pLeft = pLeft->pRight;
	pLeft->pRight = pNode;
	pNode = pLeft;
}

// Return value is the count of black children
template<typename TData>
unsigned __int64 Tree<TData>::AssertValid(const PNODE pNode, int nBlackCountSeen, int *pnBlackTotal)
{
	if (pNode == NULL)
	{
		return 0;
	}
	
	// (2)
	if (IsRed(pNode))
	{
		ASSERT(!IsRed(pNode->pLeft));
		ASSERT(!IsRed(pNode->pRight));
	}
	else
	{
		nBlackCountSeen++;
	}

	// (3)
	if ((NULL == pNode->pLeft) && (NULL == pNode->pRight))
	{
		if (*pnBlackTotal < 0)
		{
			*pnBlackTotal = nBlackCountSeen;
		}
		ASSERT(*pnBlackTotal == nBlackCountSeen);
	}

	//RND (4)
	unsigned __int64 nComputedWeight = pNode->nWeight;
	nComputedWeight += AssertValid(pNode->pLeft, nBlackCountSeen, pnBlackTotal);
	nComputedWeight += AssertValid(pNode->pRight, nBlackCountSeen, pnBlackTotal);

	ASSERT(nComputedWeight == pNode->nSummedWeight); // (5)

	return nComputedWeight;
}

// Properties being validated:
// (1) The root is black
// (2) red nodes have only black immediate children
// (3) count of black nodes on any vertical path is equal.
//RND
// (4) Weighted sum of any node is the weight of the node
//   plus the weighted sum of its children.
template<typename TData>
void Tree<TData>::AssertValid() const
{
#ifndef _DEBUG
	return true;
#else
	if (m_pRoot == NULL)
	{
		return;
	}

	// (1)
	ASSERT(m_pRoot->bRed == false);

	int nBlackPathCount = -1;
	AssertValid(m_pRoot, 0, &nBlackPathCount);

#endif
}

