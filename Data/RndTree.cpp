// RndTree.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include "Tree.h"

struct MyStuff
{
	wstring name;
	size_t nWeight;

// Operators and methods required by the tree.

	bool operator<(const MyStuff &rhs) const
	{ return name.compare(rhs.name) < 0; }

	bool operator==(const MyStuff &rhs) const
	{ return name.compare(rhs.name) == 0; }

	void OnVisit()
	{ _putws(name.c_str()); }

	size_t Weight() const
	{ return nWeight; }

// No pointer members, so default copy const/assign is fine
};

int _tmain(int argc, _TCHAR* argv[])
{
	Tree<MyStuff> tree;
	MyStuff stuff;
	const size_t MAX_STR = 60;

	srand((unsigned int)time(0L));
	stuff.name.reserve(MAX_STR);

	for (int i = 0; i < 100; i++)
	{
		stuff.name.clear();
		int len = rand() % (MAX_STR - 2);
		len += 2; // always have at least a couple chars

		for (int j = 0; j < len; j++)
		{
			// A-Z/a-z - prefer lower case 3/4 of the time

			wchar_t c = rand() % ('Z'-'A' + 1);
			c += (rand() % 4) ? 'a' : 'A';

			stuff.name.append(1, c);

			if ((rand() % 8) == 0) stuff.name.append(1, ' ');
		}
		stuff.nWeight = stuff.name.length();

		tree.Add(stuff);
		tree.AssertValid();
	}

	tree.TraverseInOrder(&MyStuff::OnVisit);
	tree.AssertValid();

	puts("\nAnd By Weight:\n");

	tree.TraverseRandom(&MyStuff::OnVisit);
	tree.AssertValid();

	tree.ResetWeights();
	tree.AssertValid();

	return 0;
}

