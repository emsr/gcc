/* { dg-do compile } */
/* { dg-options "-O -fdump-tree-veclower" } */

#define vidx(type, vec, idx) (*((type *) &(vec) + idx))
#define vector(elcount, type)  \
__attribute__((vector_size((elcount)*sizeof(type)))) type

short k;

int main (int argc, char *argv[]) {
   k = argc;
   vector(8, short) v0 = {argc,1,2,3,4,5,6,7};
   vector(8, short) v2 = {k,   k,k,k,k,k,k,k};
   vector(8, short) r1;

   r1 = v0 >> v2;

   return vidx(short, r1, 0);
}

/* { dg-final { scan-tree-dump-times ">> k.\[0-9_\]*" 1 "veclower" } } */
/* { dg-final { cleanup-tree-dump "veclower" } } */
