/* { dg-do compile } */
/* { dg-options "-fgnu-tm -fdump-tree-tmedge" } */

/* Test that a direct call to __builtin__ITM_changeTransactionMode()
   sets the irrevocable bit.  */

int global;
int george;

foo()
{
	__transaction [[relaxed]] {
		global++;
		__builtin__ITM_changeTransactionMode ();
		george++;
	}
}

/* { dg-final { scan-tree-dump-times "doesGoIrrevocable" 1 "tmedge" } } */
/* { dg-final { scan-tree-dump-times "hasNoIrrevocable" 0 "tmedge" } } */
/* { dg-final { cleanup-tree-dump "tmedge" } } */
