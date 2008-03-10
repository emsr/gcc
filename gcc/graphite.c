/* Gimple Represented as Polyhedra.
   Copyright (C) 2006, 2007 Free Software Foundation, Inc.
   Contributed by Sebastian Pop <sebastian.pop@inria.fr>.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

/* This pass converts GIMPLE to GRAPHITE, performs some loop
   transformations and then converts the resulting representation back
   to GIMPLE.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "ggc.h"
#include "tree.h"
#include "rtl.h"
#include "basic-block.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "timevar.h"
#include "cfgloop.h"
#include "tree-chrec.h"
#include "tree-data-ref.h"
#include "tree-scalar-evolution.h"
#include "tree-pass.h"
#include "domwalk.h"
#include "polylib/polylibgmp.h"
#include "cloog/cloog.h"
#include "graphite.h"


VEC (scop_p, heap) *current_scops;

static bool basic_block_simple_for_scop_p (scop_p, basic_block);
static CloogMatrix *schedule_to_scattering (graphite_bb_p);

/* Returns a new loop_to_cloog_loop_str structure.  */

static inline struct loop_to_cloog_loop_str *
new_loop_to_cloog_loop_str (unsigned int loop_num,
                            unsigned int loop_position,
                            CloogLoop *cloog_loop)
{
  struct loop_to_cloog_loop_str *result;

  result = XNEWVEC (struct loop_to_cloog_loop_str, 1);
  result->loop_num = loop_num;
  result->cloog_loop = cloog_loop;
  result->loop_position = loop_position;

  return result;
}

/* Hash function for SCOP_LOOP2CLOOG_LOOP hash table.  */

static hashval_t
hash_loop_to_cloog_loop (const void *elt)
{
  return ((const struct loop_to_cloog_loop_str *) elt)->loop_num;
}

/* Equality function for SCOP_LOOP2CLOOG_LOOP hash table.  */

static int
eq_loop_to_cloog_loop (const void *el1, const void *el2)
{
  const struct loop_to_cloog_loop_str *elt1, *elt2;

  elt1 = (const struct loop_to_cloog_loop_str *) el1;
  elt2 = (const struct loop_to_cloog_loop_str *) el2;
  return elt1->loop_num == elt2->loop_num;
}

/* Free function for SCOP_LOOP2CLOOG_LOOP.  */

static void
del_loop_to_cloog_loop (void *e)
{
  free (e);
}

/* Print the schedules from SCHED.  */

void
print_graphite_bb (FILE *file, graphite_bb_p gb, int indent, int verbosity)
{
  fprintf (file, "\nGBB (\n");

  fprintf (file, "       (static schedule: ");
  print_lambda_vector (file, GBB_STATIC_SCHEDULE (gb), scop_nb_loops (GBB_SCOP (gb)) + 1);
  fprintf (file, "       )\n");

  print_loops_bb (file, GBB_BB (gb), indent+2, verbosity);

  if (GBB_DATA_REFS (gb))
    dump_data_references (file, GBB_DATA_REFS (gb));

  fprintf (file, "       (scattering: \n");
  cloog_matrix_print (file, schedule_to_scattering (gb));
  fprintf (file, "       )\n");

  fprintf (file, ")\n");
}

/* Print SCOP to FILE.  */

static void
print_scop (FILE *file, scop_p scop, int verbosity)
{
  if (scop == NULL)
    return;

  fprintf (file, "\nSCoP_%d_%d (\n",
	   SCOP_ENTRY (scop)->index, SCOP_EXIT (scop)->index);

  fprintf (file, "       (cloog: \n");
  cloog_program_print (file, SCOP_PROG (scop));
  fprintf (file, "       )\n");

  if (scop->bbs)
    {
      graphite_bb_p gb;
      unsigned i;

      for (i = 0; VEC_iterate (graphite_bb_p, scop->bbs, i, gb); i++)
	print_graphite_bb (file, gb, 0, verbosity);
    }

  fprintf (file, ")\n");
}

/* Print all the SCOPs to FILE.  */

static void
print_scops (FILE *file, int verbosity)
{
  unsigned i;
  scop_p scop;

  for (i = 0; VEC_iterate (scop_p, current_scops, i, scop); i++)
    print_scop (file, scop, verbosity);
}

/* Debug SCOP.  */

void
debug_scop (scop_p scop, int verbosity)
{
  print_scop (stderr, scop, verbosity);
}

/* Debug all SCOPs from CURRENT_SCOPS.  */

void 
debug_scops (int verbosity)
{
  print_scops (stderr, verbosity);
}

/* Return true when BB is contained in SCOP.  */

static inline bool
bb_in_scop_p (basic_block bb, scop_p scop)
{
  return bitmap_bit_p (SCOP_BBS_B (scop), bb->index);
}

/* Pretty print a scop in DOT format.  */

static void 
dot_scop_1 (FILE *file, scop_p scop)
{
  edge e;
  edge_iterator ei;
  basic_block bb;
  basic_block entry = SCOP_ENTRY (scop);
  basic_block exit = SCOP_EXIT (scop);
    
  fprintf (file, "digraph SCoP_%d_%d {\n", entry->index,
	   exit->index);

  FOR_ALL_BB (bb)
    {
      if (bb == entry)
	fprintf (file, "%d [shape=triangle];\n", bb->index);

      if (bb == exit)
	fprintf (file, "%d [shape=box];\n", bb->index);

      if (bb_in_scop_p (bb, scop)) 
	fprintf (file, "%d [color=red];\n", bb->index);

      FOR_EACH_EDGE (e, ei, bb->succs)
	fprintf (file, "%d -> %d;\n", bb->index, e->dest->index);
    }

  fputs ("}\n\n", file);
}

/* Display SCOP using dotty.  */

void
dot_scop (scop_p scop)
{
  FILE *stream = fopen ("/tmp/scop.dot", "w");
  gcc_assert (stream != NULL);

  dot_scop_1 (stream, scop);
  fclose (stream);

  system ("dotty /tmp/scop.dot");
}

/* Pretty print all SCoPs in DOT format and mark them with different colors.
   If there are not enough colors (8 at the moment), paint later SCoPs gray.
   Special nodes:
   - filled node: entry of a SCoP,
   - node shaped like a box: end of a SCoP,
   - node with diagonals: critical BB.  */

static void
dot_all_scops_1 (FILE *file)
{
  basic_block bb;
  edge e;
  edge_iterator ei;
  scop_p scop;
  const char* color;
  int i;

  /* Disable debugging while printing graph.  */
  int tmp_dump_flags = dump_flags;
  dump_flags = 0;

  fprintf (file, "digraph all {\n");

  FOR_ALL_BB (bb)
    {

      /* Use HTML for every bb label.  So we are able to print bbs
         which are part of two different SCoPs, with two different
         background colors.  */
      fprintf (file, "%d [label=<<TABLE BORDER=\"0\" CELLBORDER=\"1\" ",
                     bb->index);
      fprintf (file, "CELLSPACING=\"0\"> ");

      /* Select color for SCoP.  */
      for (i = 0; VEC_iterate (scop_p, current_scops, i, scop); i++)
	if (bb_in_scop_p (bb, scop) || scop->exit == bb || scop->entry == bb)
	  {
	    switch (i)
	      {
	      case 0: /* red */
		color = "#e41a1c";
		break;
	      case 1: /* blue */
		color = "#377eb8";
		break;
	      case 2: /* green */
		color = "#4daf4a";
		break;
	      case 3: /* purple */
		color = "#984ea3";
		break;
	      case 4: /* orange */
		color = "#ff7f00";
		break;
	      case 5: /* yellow */
		color = "#ffff33";
		break;
	      case 6: /* brown */
		color = "#e41a1c";
		break;
	      case 7: /* rose */
		color = "#f781bf";
		break;
	      default: /* gray */
		color = "#e41a1c";
	      }

	    fprintf (file, "  <TR><TD WIDTH=\"50\" BGCOLOR=\"%s\">", color);

	    if (bb == scop->entry && bb == scop->exit)
	      fprintf (file, " %d*# </TD></TR> ", bb->index);
	    else if (bb == scop->entry)
	      fprintf (file, " %d* </TD></TR> ", bb->index);
	    else if (bb == scop->exit)
	      fprintf (file, " %d# </TD></TR> ", bb->index);
	    else
	      fprintf (file, " %d </TD></TR> ", bb->index);
	  }

      fprintf (file, "</TABLE>>, shape=box, style=\"setlinewidth(0)\"]\n");
    }

  /* Print edges and mark blocks not allowed in SCoP.  */
  FOR_ALL_BB (bb)
    {
      FOR_EACH_EDGE (e, ei, bb->succs)
	fprintf (file, "%d -> %d;\n", bb->index, e->dest->index);

      for (i = 0; VEC_iterate (scop_p, current_scops, i, scop); i++)
	if (bb_in_scop_p (bb, scop) || scop->exit == bb || scop->entry == bb)
	  if (!basic_block_simple_for_scop_p (scop, bb))
	    fprintf (file, "%d [color=\"#000000\", style=filled]\n",
		     bb->index);
    }

  fputs ("}\n\n", file);

  /* Enable debugging again.  */
  dump_flags = tmp_dump_flags;
}

/* Display all SCoPs using dotty.  */

void
dot_all_scops (void)
{
  FILE *stream = fopen ("/tmp/allscops.dot", "w");
  gcc_assert (stream != NULL);

  dot_all_scops_1(stream);
  fclose (stream);

  system ("dotty /tmp/allscops.dot");
}



/* Returns true when LOOP is in SCOP.  */

static inline bool 
loop_in_scop_p (struct loop *loop, scop_p scop)
{
  return (bb_in_scop_p (loop->header, scop)
	  && bb_in_scop_p (loop->latch, scop));
}

/* Returns the outermost loop in SCOP that contains BB.  */

static struct loop *
outermost_loop_in_scop (scop_p scop, basic_block bb)
{
  struct loop *nest;

  nest = bb->loop_father;
  while (loop_outer (nest) && loop_in_scop_p (loop_outer (nest), scop))
    nest = loop_outer (nest);

  return nest;
}

/* Return true when EXPR is an affine function in LOOP for the current
   open scop.  EXPR is contained in BB.  */

static bool
scop_affine_expr (scop_p scop, struct loop *loop, tree expr, basic_block bb)
{
  tree scev = analyze_scalar_evolution (loop, expr);
  struct loop *outermost_loop = outermost_loop_in_scop (scop, bb);

  scev = instantiate_parameters (outermost_loop, scev);

  return (evolution_function_is_invariant_p (scev, outermost_loop->num)
	  || evolution_function_is_affine_multivariate_p (scev,
							  outermost_loop->num));
}


/* Return true only when STMT is simple enough for being handled by
   GRAPHITE.  */

static bool
stmt_simple_for_scop_p (scop_p scop, tree stmt)
{
  basic_block bb = bb_for_stmt (stmt);
  struct loop *loop = bb->loop_father;

  /* ASM_EXPR and CALL_EXPR may embed arbitrary side effects.
     Calls have side-effects, except those to const or pure
     functions.  */
  if ((TREE_CODE (stmt) == CALL_EXPR
       && !(call_expr_flags (stmt) & (ECF_CONST | ECF_PURE)))
      || (TREE_CODE (stmt) == ASM_EXPR
	  && ASM_VOLATILE_P (stmt)))
    return false;

  switch (TREE_CODE (stmt))
    {
    case RETURN_EXPR:
    case LABEL_EXPR:
      return true;

    case COND_EXPR:
      {
	tree opnd0 = TREE_OPERAND (stmt, 0);

	switch (TREE_CODE (opnd0))
	  {
	  case NE_EXPR:
	  case EQ_EXPR:
	  case LT_EXPR:
	  case GT_EXPR:
	  case LE_EXPR:
	  case GE_EXPR:
	    return (scop_affine_expr (scop, loop, TREE_OPERAND (opnd0, 0), bb)
		    && scop_affine_expr (scop, loop, TREE_OPERAND (opnd0, 1), bb));
	  default:
	    return false;
	  }
      }

    case GIMPLE_MODIFY_STMT:
      {
	tree opnd0 = GIMPLE_STMT_OPERAND (stmt, 0);
	tree opnd1 = GIMPLE_STMT_OPERAND (stmt, 1);

	if (TREE_CODE (opnd0) == ARRAY_REF 
	     || TREE_CODE (opnd0) == INDIRECT_REF
	     || TREE_CODE (opnd0) == COMPONENT_REF)
	  {
	    data_reference_p dr;

	    dr = create_data_ref (loop, opnd0, stmt, false);
	    if (!dr)
	      return false;

	    free_data_ref (dr);

	    if (TREE_CODE (opnd1) == ARRAY_REF 
		|| TREE_CODE (opnd1) == INDIRECT_REF
		|| TREE_CODE (opnd1) == COMPONENT_REF)
	      {
		dr = create_data_ref (loop, opnd1, stmt, true);
		if (!dr)
		  return false;

		free_data_ref (dr);
		return true;
	      }
	  }

	if (TREE_CODE (opnd1) == ARRAY_REF 
	     || TREE_CODE (opnd1) == INDIRECT_REF
	     || TREE_CODE (opnd1) == COMPONENT_REF)
	  {
	    data_reference_p dr;

	    dr = create_data_ref (loop, opnd1, stmt, true);
	    if (!dr)
	      return false;

	    free_data_ref (dr);
	    return true;
	  }

	/* We cannot return (scop_affine_expr (loop, opnd0) &&
	   scop_affine_expr (loop, opnd1)) because D.1882_16 is
	   not affine in the following:

	   D.1881_15 = a[j_13][pretmp.22_20];
	   D.1882_16 = D.1881_15 + 2;
	   a[j_22][i_12] = D.1882_16;

	   but this is valid code in a scop.

	   FIXME: I'm not yet 100% sure that returning true is safe:
	   there might be exponential scevs.  On the other hand, if
	   these exponential scevs do not reference arrays, then
	   access functions, domains and schedules remain affine.  So
	   it is very well possible that we can handle exponential
	   scalar computations.  */
	return true;
      }

    case CALL_EXPR:
      {
	tree args;

	for (args = TREE_OPERAND (stmt, 1); args; args = TREE_CHAIN (args))
	  if (TREE_CODE (TREE_VALUE (args)) == ARRAY_REF
	      || TREE_CODE (TREE_VALUE (args)) == INDIRECT_REF
	      || TREE_CODE (TREE_VALUE (args)) == COMPONENT_REF)
	    {
	      data_reference_p dr;

	      dr = create_data_ref (loop, TREE_VALUE (args), stmt, true);
	      if (!dr)
		return false;

	      free_data_ref (dr);
	    }

	return true;
      }

    default:
      /* These nodes cut a new scope.  */
      return false;
    }

  return false;
}

/* Returns true if BB contains only simple statements with respect
   to SCOP.  */

static bool
basic_block_simple_for_scop_p (scop_p scop, basic_block bb)
{
  block_stmt_iterator bsi;

  for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
    if (!stmt_simple_for_scop_p (scop, bsi_stmt (bsi)))
      return false;

  return true;
}

/* Creates a new scop starting with ENTRY.  */

static scop_p
new_scop (basic_block entry)
{
  scop_p scop = XNEW (struct scop);

  if (0)
    fprintf (stdout, "open_scop (%d)\n", entry->index);

  SCOP_ENTRY (scop) = entry;
  SCOP_BBS (scop) = VEC_alloc (graphite_bb_p, heap, 3);
  SCOP_BBS_B (scop) = BITMAP_ALLOC (NULL);
  SCOP_LOOPS (scop) = BITMAP_ALLOC (NULL);
  SCOP_LOOP_NEST (scop) = VEC_alloc (loop_p, heap, 3);
  SCOP_PARAMS (scop) = VEC_alloc (tree, heap, 3);
  SCOP_PROG (scop) = cloog_program_malloc ();
  SCOP_PROG (scop)->names = cloog_names_malloc ();
  SCOP_LOOP2CLOOG_LOOP (scop) = htab_create (10, hash_loop_to_cloog_loop,
					     eq_loop_to_cloog_loop,
					     del_loop_to_cloog_loop);
  return scop;
}

/* Deletes the scop.  */

static void
free_scop (scop_p scop)
{
  VEC_free (graphite_bb_p, heap, SCOP_BBS (scop));
  BITMAP_FREE (SCOP_BBS_B (scop));
  BITMAP_FREE (SCOP_LOOPS (scop));
  VEC_free (loop_p, heap, SCOP_LOOP_NEST (scop));
  VEC_free (tree, heap, SCOP_PARAMS (scop));
  cloog_program_free (SCOP_PROG (scop));
  htab_delete (SCOP_LOOP2CLOOG_LOOP (scop)); 
  free (scop);
}

static void
free_scops (VEC (scop_p, heap) *scops)
{
  unsigned i;
  scop_p scop;

  for (i = 0; VEC_iterate (scop_p, scops, i, scop); i++)
    free_scop (scop);

  VEC_free (scop_p, heap, scops);
}

/* Save the SCOP.  */

static inline void
save_scop (scop_p scop)
{
  VEC_safe_push (scop_p, heap, current_scops, scop);
}

/* Returns true when the predecessors of BB are at the same loop depth as BB.  */

static bool
preds_at_same_depth (basic_block bb)
{
  edge e;
  edge_iterator ei;
  unsigned entry_depth = loop_depth (bb->loop_father);

  FOR_EACH_EDGE (e, ei, bb->preds)
    if (loop_depth (e->src->loop_father) != entry_depth)
      return false;

  return true;
}

/* Returns true when the successors of BB are at the same loop depth as BB.  */

static bool
succs_at_same_depth (basic_block bb)
{
  edge e;
  edge_iterator ei;
  unsigned entry_depth = loop_depth (bb->loop_father);

  FOR_EACH_EDGE (e, ei, bb->succs)
    if (loop_depth (e->dest->loop_father) != entry_depth)
      return false;

  return true;
}

/* Build the SCoP ending with EXIT.  */

static void
end_scop (scop_p scop, basic_block exit, VEC (scop_p, heap) **open_scops)
{
  if (0)
    fprintf (stdout, "close_scop (%d, %d) \n", SCOP_ENTRY (scop)->index, exit->index);

  /* Invariant: the SCoP exit should be dominated by its entry, and
     the SCoP entry should be postdominated by the exit, unless the
     exit is an exceptional one, that is a block that cannot be
     represented in graphite.  */
  gcc_assert ((dominated_by_p (CDI_DOMINATORS, exit, SCOP_ENTRY (scop))
	       || !basic_block_simple_for_scop_p (scop, SCOP_ENTRY (scop)))
	      && (dominated_by_p (CDI_POST_DOMINATORS, SCOP_ENTRY (scop), exit)
		  || !basic_block_simple_for_scop_p (scop, exit)));

  if (VEC_length (edge, SCOP_ENTRY (scop)->succs) >= 2
      && succs_at_same_depth (SCOP_ENTRY (scop))
      && (!dominated_by_p (CDI_DOMINATORS, exit, SCOP_ENTRY (scop))
	  || !dominated_by_p (CDI_POST_DOMINATORS, SCOP_ENTRY (scop), exit)))
    scop = new_scop (SCOP_ENTRY (scop));
  else
    VEC_pop (scop_p, *open_scops);

  SCOP_EXIT (scop) = exit;
  save_scop (scop);
}

/* Add to OPEN_SCOPS the dominators that don't dominate the LAST scop.
   First elements in OPEN_SCOPS dominate the remaining elements.  */

static void
add_dominators_to_open_scops (VEC (scop_p, heap) **open_scops,
			      scop_p last, basic_block bb)
{
  basic_block dom = get_immediate_dominator (CDI_DOMINATORS, bb);

  while (dominated_by_p (CDI_DOMINATORS, dom, SCOP_ENTRY (last))
	 && VEC_length (edge, dom->succs) < 2
	 && VEC_length (edge, dom->preds) < 2
	 && dom != SCOP_ENTRY (last))
    dom = get_immediate_dominator (CDI_DOMINATORS, dom);

  if (dom == SCOP_ENTRY (last))
    return;

  if (dominated_by_p (CDI_DOMINATORS, dom, SCOP_ENTRY (last)))
    {
      /* Add outer dominators first.  */
      add_dominators_to_open_scops (open_scops, last, dom);

      /* Add a scop from last dominator to the current one.  */
      last = VEC_last (scop_p, *open_scops);
      end_scop (last, dom, open_scops);

      /* Push an open scop for branches originating from DOM.  */
      VEC_safe_push (scop_p, heap, *open_scops, new_scop (dom));
    }
}

/* Mark difficult constructs as boundaries of SCoPs.  Callback for
   walk_dominator_tree.  */

static bool
test_for_scop_bound (const_basic_block cbb, const void *data)
{
  basic_block bb = (basic_block) cbb;
  VEC (scop_p, heap) **open_scops = (VEC (scop_p, heap) **) data;
  scop_p last = VEC_last (scop_p, *open_scops);

  if (!basic_block_simple_for_scop_p (last, bb))
    {
      if (0)
	fprintf (stdout, "harmful_bb (%d) \n", bb->index);

      add_dominators_to_open_scops (open_scops, last, bb);

      /* Add a scop from last dominator to the harmful BB.  */
      last = VEC_last (scop_p, *open_scops);
      end_scop (last, bb, open_scops);

      /* Push an open scop for all the code after the harmful BB.  */
      VEC_safe_push (scop_p, heap, *open_scops, new_scop (bb));

      if (debug_p ())
	fprintf (dump_file, "difficult bound bb_%d\n\n", bb->index);

      return true;
    }

  /* The current BB closes several open scops when it postdominates
     at least the last two open scops.  */
  /* A merge of several branches.  */
  if (VEC_length (scop_p, *open_scops) >= 2
      && VEC_length (edge, bb->preds) >= 2
      && preds_at_same_depth (bb))
    {
      scop_p before_last = VEC_index (scop_p, *open_scops,
				      VEC_length (scop_p, *open_scops) - 2);

      if (dominated_by_p (CDI_POST_DOMINATORS, SCOP_ENTRY (before_last), bb)
	  && dominated_by_p (CDI_POST_DOMINATORS, SCOP_ENTRY (last), bb))
	while (dominated_by_p (CDI_POST_DOMINATORS, SCOP_ENTRY (last), bb))
	  {
	    end_scop (last, bb, open_scops);
	    if (VEC_length (scop_p, *open_scops) > 0)
	      last = VEC_last (scop_p, *open_scops);
	    else
	      break;
	  }

      /* Push an open scop for all the code after BB.  */
      VEC_safe_push (scop_p, heap, *open_scops, new_scop (bb));

      return true;
    }

  /* A loop exit ends a scop when the scop entry is in the loop.  */
  if (VEC_length (edge, bb->succs) >= 2)
    {
      edge e;
      edge_iterator ei;
      unsigned entry_depth = loop_depth (SCOP_ENTRY (last)->loop_father);

      FOR_EACH_EDGE (e, ei, bb->succs)
	{
	  if (loop_depth (e->dest->loop_father) < entry_depth)
	    {
	      end_scop (last, bb, open_scops);
	      VEC_safe_push (scop_p, heap, *open_scops, new_scop (bb));
	    }
	}
    }

  return true;
}

/* Find static control parts.  */

static void
build_scops (void)
{
  basic_block *bbs = XCNEWVEC (basic_block, n_basic_blocks);
  scop_p last;
  VEC (scop_p, heap) *open_scops = VEC_alloc (scop_p, heap, 3);

  VEC_safe_push (scop_p, heap, open_scops, new_scop (ENTRY_BLOCK_PTR->next_bb));

  dfs_enumerate_from (ENTRY_BLOCK_PTR, 0, test_for_scop_bound, bbs,
		      n_basic_blocks, &open_scops);

  free (bbs);
  last = VEC_last (scop_p, open_scops);
  end_scop (last, EXIT_BLOCK_PTR->prev_bb, &open_scops);

  gcc_assert (VEC_empty (scop_p, open_scops));
  VEC_free (scop_p, heap, open_scops);
}

/* Store the GRAPHITE representation of BB.  */

static void
build_graphite_bb (scop_p scop, basic_block bb)
{
  struct graphite_bb *gb;

  /* Build the new representation for the basic block.  */
  gb = XNEW (struct graphite_bb);
  GBB_BB (gb) = bb;
  GBB_SCOP (gb) = scop;
  GBB_DATA_REFS (gb) = NULL; 

  /* Store the GRAPHITE representation of the current BB.  */
  VEC_safe_push (graphite_bb_p, heap, scop->bbs, gb);
  bitmap_set_bit (SCOP_BBS_B (scop), bb->index);
}

/* Predicate for dfs order traversal of bbs in a scop.  */

static bool
dfs_bb_in_scop_p (const_basic_block bb, const void *data)
{
  scop_p scop = (scop_p) data;

  /* Scop's exit is not in the scop.  */
  if (bb == SCOP_EXIT (scop)
      /* Every block in the scop is dominated by scop's entry.  */
      || !dominated_by_p (CDI_DOMINATORS, bb, SCOP_ENTRY (scop))
      /* Every block in the scop is postdominated by scop's exit.  */
      || !dominated_by_p (CDI_POST_DOMINATORS, bb, SCOP_EXIT (scop)))
    return false;

  build_graphite_bb (scop, (basic_block) bb);
  return true;
}

/* Gather the basic blocks belonging to the SCOP.  */

static void
build_scop_bbs (scop_p scop)
{
  basic_block *bbs = XCNEWVEC (basic_block, n_basic_blocks);

  /* Iterate over all the basic blocks of the scop in their pseudo
     execution order, and associate to each bb a static schedule.
     (pseudo exec order = the branches of a condition are scheduled
     sequentially: the then clause comes before the else clause.)  */

  dfs_enumerate_from (SCOP_ENTRY (scop), 0, dfs_bb_in_scop_p, bbs,
		      n_basic_blocks, scop);

  free (bbs);
}

/* Record LOOP as occuring in SCOP.  */

static void
scop_record_loop (scop_p scop, struct loop *loop)
{
  if (!bitmap_bit_p (SCOP_LOOPS (scop), loop->num))
    {
      bitmap_set_bit (SCOP_LOOPS (scop), loop->num);
      VEC_safe_push (loop_p, heap, SCOP_LOOP_NEST (scop), loop);
    }
}

/* Build the loop nests contained in SCOP.  */

static void
build_scop_loop_nests (scop_p scop)
{
  unsigned i;
  graphite_bb_p gb;

  for (i = 0; VEC_iterate (graphite_bb_p, SCOP_BBS (scop), i, gb); i++)
    scop_record_loop (scop, gbb_loop (gb));
}

/* Build for BB the static schedule.  */

static void
build_scop_canonical_schedules (scop_p scop)
{
  unsigned i;
  graphite_bb_p gb;
  unsigned nb = scop_nb_loops (scop) + 1;

  SCOP_STATIC_SCHEDULE (scop) = lambda_vector_new (nb);

  for (i = 0; VEC_iterate (graphite_bb_p, SCOP_BBS (scop), i, gb); i++)
    {
      SCOP_STATIC_SCHEDULE (scop)[scop_loop_index (scop, gbb_loop (gb))] += 1;
      GBB_STATIC_SCHEDULE (gb) = lambda_vector_new (nb);
      lambda_vector_copy (SCOP_STATIC_SCHEDULE (scop), 
			  GBB_STATIC_SCHEDULE (gb), nb);
    }
}

/* Return true when STMT is contained in SCOP.  */

static inline bool
stmt_in_scop_p (tree stmt, scop_p scop)
{
  return bb_in_scop_p (bb_for_stmt (stmt), scop);
}

static inline bool
function_parameter_p (tree var)
{
  return (TREE_CODE (SSA_NAME_VAR (var)) == PARM_DECL);
}

/* Return true when VAR is invariant in SCOP.  In that case, VAR is a
   parameter of SCOP.  */

static inline bool
invariant_in_scop_p (tree var, scop_p scop)
{
  gcc_assert (TREE_CODE (var) == SSA_NAME);

  return (function_parameter_p (var)
	  || !stmt_in_scop_p (SSA_NAME_DEF_STMT (var), scop));
}

/* Get the index corresponding to VAR in the current LOOP.  */

static int
param_index (tree var, scop_p scop)
{
  int i;
  tree p;

  gcc_assert (TREE_CODE (var) == SSA_NAME);

  for (i = 0; VEC_iterate (tree, SCOP_PARAMS (scop), i, p); i++)
    if (p == var)
      return i;

  VEC_safe_push (tree, heap, SCOP_PARAMS (scop), var);
  return VEC_length (tree, SCOP_PARAMS (scop)) - 1;
}

struct irp_data
{
  struct loop *loop;
  scop_p scop;
};

/* Record VAR as a parameter of DTA->scop.  */

static bool
idx_record_param (tree *var, void *dta)
{
  struct irp_data *data = dta;

  switch (TREE_CODE (*var))
    {
    case SSA_NAME:
      if (invariant_in_scop_p (*var, data->scop))
	param_index (*var, data->scop);
      return true;

    default:
      return true;
    }
}

/* Record parameters occurring in an evolution function of a data
   access, niter expression, etc.  Callback for for_each_index.  */

static bool
idx_record_params (tree base, tree *idx, void *dta)
{
  struct irp_data *data = dta;

  if (TREE_CODE (base) != ARRAY_REF)
    return true;

  if (TREE_CODE (*idx) == SSA_NAME)
    {
      tree scev;
      scop_p scop = data->scop;
      struct loop *loop = data->loop;
      struct loop *floop = SCOP_ENTRY (scop)->loop_father;

      scev = analyze_scalar_evolution (loop, *idx);
      scev = instantiate_parameters (floop, scev);
      for_each_scev_op (&scev, idx_record_param, dta);
    }

  return true;
}

/* Helper function for walking in dominance order basic blocks.  Find
   parameters with respect to SCOP in memory access functions used in
   BB.  DW_DATA contains the context and the results for extract
   parameters information.  */

static void
find_params_in_bb (struct dom_walk_data *dw_data, basic_block bb)
{
  unsigned i;
  data_reference_p dr;
  VEC (data_reference_p, heap) *drs;
  block_stmt_iterator bsi;
  scop_p scop = (scop_p) dw_data->global_data;
  struct loop *nest = outermost_loop_in_scop (scop, bb);

  /* Find the parameters used in the memory access functions.  */
  drs = VEC_alloc (data_reference_p, heap, 5);
  for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
    find_data_references_in_stmt (nest, bsi_stmt (bsi), &drs);

  for (i = 0; VEC_iterate (data_reference_p, drs, i, dr); i++)
    {
      struct irp_data irp;

      irp.loop = bb->loop_father;
      irp.scop = scop;
      for_each_index (&dr->ref, idx_record_params, &irp);
    }

  VEC_free (data_reference_p, heap, drs);
}

/* Initialize Cloog's parameter names from the names used in GIMPLE.
   Initialize Cloog's iterator names, using 'graphite_iterator_%d'
   from 0 to scop_nb_loops (scop).  */

static void
initialize_cloog_names (scop_p scop)
{
  unsigned i, nb_params = VEC_length (tree, SCOP_PARAMS (scop));
  char **params = XNEWVEC (char *, nb_params);
  unsigned nb_iterators = scop_nb_loops(scop);
  unsigned nb_scattering= scop_nb_loops(scop) * 2 + 1;
  char **iterators = XNEWVEC (char *, nb_iterators);
  char **scattering = XNEWVEC (char *, nb_scattering);
  tree p;

  for (i = 0; VEC_iterate (tree, SCOP_PARAMS (scop), i, p); i++)
    {
      const char *name = get_name (SSA_NAME_VAR (p));

      if (name)
	{
	  params[i] = XNEWVEC (char, strlen (name) + 12);
	  sprintf (params[i], "%s_%d", name, SSA_NAME_VERSION (p));
	}
      else
	{
	  params[i] = XNEWVEC (char, 12);
	  sprintf (params[i], "T_%d", SSA_NAME_VERSION (p));
	}
    }

  SCOP_PROG (scop)->names->nb_parameters = nb_params;
  SCOP_PROG (scop)->names->parameters = params;

  for (i = 0; i < nb_iterators; i++)
  {
    iterators[i] = XNEWVEC (char, 18 + 12);
    sprintf (iterators[i], "graphite_iterator_%d", i);
  }

  SCOP_PROG (scop)->names->nb_iterators = nb_iterators;
  SCOP_PROG (scop)->names->iterators = iterators;

  for (i = 0; i < nb_scattering; i++)

  {
    scattering[i] = XNEWVEC (char, 2 + 12);
    sprintf (scattering[i], "s_%d", i);
  }

  SCOP_PROG (scop)->names->nb_scattering = nb_scattering;
  SCOP_PROG (scop)->names->scattering = scattering;
}

/* Record the parameters used in the SCOP.  A variable is a parameter
   in a scop if it does not vary during the execution of that scop.  */

static void
find_scop_parameters (scop_p scop)
{
  unsigned i;
  struct dom_walk_data walk_data;
  struct loop *loop;

  /* Find the parameters used in the loop bounds.  */
  for (i = 0; VEC_iterate (loop_p, SCOP_LOOP_NEST (scop), i, loop); i++)
    {
      tree nb_iters = number_of_latch_executions (loop);

      if (chrec_contains_symbols (nb_iters))
	{
	  struct irp_data irp;

	  irp.loop = loop;
	  irp.scop = scop;
	  for_each_scev_op (&nb_iters, idx_record_param, &irp);
	}
    }

  /* Find the parameters used in data accesses.  */
  memset (&walk_data, 0, sizeof (struct dom_walk_data));
  walk_data.before_dom_children_before_stmts = find_params_in_bb;
  walk_data.dom_direction = CDI_DOMINATORS;
  walk_data.global_data = scop;
  init_walk_dominator_tree (&walk_data);
  walk_dominator_tree (&walk_data, SCOP_ENTRY (scop));
  fini_walk_dominator_tree (&walk_data);

  initialize_cloog_names (scop);
}

/* Returns the number of parameters for SCOP.  */

static inline unsigned
nb_params_in_scop (scop_p scop)
{
  return VEC_length (tree, SCOP_PARAMS (scop));
}

/* Build the context constraints for SCOP: constraints and relations
   on parameters.  */

static void
build_scop_context (scop_p scop)
{
  unsigned nb_params = nb_params_in_scop (scop);
  CloogMatrix *matrix = cloog_matrix_alloc (1, nb_params + 2);

  /* Insert '0 >= 0' in the context matrix, as it is not allowed to be
     empty. */
 
  value_init (matrix->p[0][0]);
  value_set_si (matrix->p[0][0], 1);

  value_init (matrix->p[0][nb_params + 1]);
  value_set_si (matrix->p[0][nb_params + 1], 0);

  SCOP_PROG (scop)->context = cloog_domain_matrix2domain (matrix);
}

/* Scan EXPR and translate it to an inequality vector INEQ that will
   be inserted in the constraint domain matrix C at row R.  N is
   the number of columns for loop iterators in C.  */

static void
scan_tree_for_params (scop_p s, tree e, CloogMatrix *c, int r, int n, Value k)
{
  unsigned cst_col, param_col;

  switch (TREE_CODE (e))
    {
    case MULT_EXPR:
      if (chrec_contains_symbols (TREE_OPERAND (e, 0)))
	{
	  Value val;

	  gcc_assert (host_integerp (TREE_OPERAND (e, 1), 1));

	  value_init (val);
	  value_set_si (val, int_cst_value (TREE_OPERAND (e, 1)));
	  value_multiply (k, k, val);
	  value_clear (val);
	  scan_tree_for_params (s, TREE_OPERAND (e, 0), c, r, n, k);
	}
      else
	{
	  Value val;

	  gcc_assert (host_integerp (TREE_OPERAND (e, 0), 1));

	  value_init (val);
	  value_set_si (val, int_cst_value (TREE_OPERAND (e, 0)));
	  value_multiply (k, k, val);
	  value_clear (val);
	  scan_tree_for_params (s, TREE_OPERAND (e, 1), c, r, n, k);
	}
      break;

    case PLUS_EXPR:
      scan_tree_for_params (s, TREE_OPERAND (e, 0), c, r, n, k);
      scan_tree_for_params (s, TREE_OPERAND (e, 1), c, r, n, k);
      break;

    case MINUS_EXPR:
      scan_tree_for_params (s, TREE_OPERAND (e, 0), c, r, n, k);
      value_oppose (k, k);
      scan_tree_for_params (s, TREE_OPERAND (e, 1), c, r, n, k);
      break;

    case SSA_NAME:
      param_col = 1 + n + param_index (e, s);
      value_init (c->p[r][param_col]);
      value_assign (c->p[r][param_col], k);
      break;

    case INTEGER_CST:
      cst_col = c->NbColumns - 1;
      value_init (c->p[r][cst_col]);
      value_set_si (c->p[r][cst_col], int_cst_value (e));
      break;

    case NOP_EXPR:
    case CONVERT_EXPR:
    case NON_LVALUE_EXPR:
      scan_tree_for_params (s, TREE_OPERAND (e, 0), c, r, n, k);
      break;

    default:
      break;
    }
}

/* Returns the first loop in SCOP, returns NULL if there is no loop in
   SCOP.  */

static struct loop *
first_loop_in_scop (scop_p scop)
{
  struct loop *loop = SCOP_ENTRY (scop)->loop_father->inner;

  if (loop == NULL)
    return NULL;

  while (loop->next && !loop_in_scop_p (loop, scop))
    loop = loop->next;

  if (loop_in_scop_p (loop, scop))
    return loop;

  return NULL;
}

/* Calculate the number of loops around GB in the current SCOP.  */

static inline int
nb_loops_around_gb (graphite_bb_p gb)
{
  scop_p scop = GBB_SCOP (gb);
  struct loop *l = gbb_loop (gb);
  int d = 0;

  for (; loop_in_scop_p (l, scop); d++, l = loop_outer (l));

  return d;
}

/* Converts LOOP in SCOP to cloog's format.  NB_OUTER_LOOPS is the
   number of loops surrounding LOOP in SCOP.  OUTER_CSTR gives the
   constraints matrix for the surrounding loops.  */

static CloogLoop *
setup_cloog_loop (scop_p scop, struct loop *loop, CloogMatrix *outer_cstr,
		  int nb_outer_loops)
{
  unsigned i, j, row;
  CloogStatement *statement;
  CloogMatrix *cstr;
  loop_to_cloog_loop tmp = XNEWVEC (struct loop_to_cloog_loop_str, 1);
  PTR *slot;
  CloogLoop *res = cloog_loop_malloc ();

  unsigned nb_rows = outer_cstr->NbRows + 1;
  unsigned nb_cols = outer_cstr->NbColumns + 1;

  /* Last column of CSTR is the column of constants.  */
  unsigned cst_col = nb_cols - 1;

  /* The column for the current loop is just after the columns of
     other outer loops.  */
  unsigned loop_col = nb_outer_loops + 1;

  tree nb_iters = number_of_latch_executions (loop);

  /* When the number of iterations is a constant or a parameter, we
     add a constraint for the upper bound of the loop.  So add a row
     to the constraint matrix before allocating it.  */
  if (TREE_CODE (nb_iters) == INTEGER_CST
      || !chrec_contains_undetermined (nb_iters))
    nb_rows++;

  cstr = cloog_matrix_alloc (nb_rows, nb_cols);

  /* Copy the outer constraints.  */
  for (i = 0; i < outer_cstr->NbRows; i++)
    {
      /* Copy the eq/ineq and loops columns.  */
      for (j = 0; j < loop_col; j++)
	{
	  value_init (cstr->p[i][j]);
	  value_assign (cstr->p[i][j], outer_cstr->p[i][j]);
	}

      /* Leave an empty column in CSTR for the current loop, and then
	 copy the parameter columns.  */
      for (j = loop_col; j < outer_cstr->NbColumns - 1; j++)
	{
	  value_init (cstr->p[i][j + 1]);
	  value_assign (cstr->p[i][j + 1], outer_cstr->p[i][j]);
	}

      /* Copy the constant column.  */
      value_init (cstr->p[i][cst_col]);
      value_assign (cstr->p[i][cst_col], 
		    outer_cstr->p[i][outer_cstr->NbColumns - 1]);
    }

  /* 0 <= loop_i */
  row = outer_cstr->NbRows;
  value_init (cstr->p[row][0]);
  value_set_si (cstr->p[row][0], 1);
  value_init (cstr->p[row][loop_col]);
  value_set_si (cstr->p[row][loop_col], 1);

  /* loop_i <= nb_iters */
  if (TREE_CODE (nb_iters) == INTEGER_CST)
    {
      row++;
      value_init (cstr->p[row][0]);
      value_set_si (cstr->p[row][0], 1);
      value_init (cstr->p[row][loop_col]);
      value_set_si (cstr->p[row][loop_col], -1);

      value_init (cstr->p[row][cst_col]);
      value_set_si (cstr->p[row][cst_col],
		    int_cst_value (nb_iters));
    }
  else if (!chrec_contains_undetermined (nb_iters))
    {
      /* Otherwise nb_iters contains parameters: scan the nb_iters
	 expression and build its matrix representation.  */
      Value one;

      row++;
      value_init (cstr->p[row][0]);
      value_set_si (cstr->p[row][0], 1);
      value_init (cstr->p[row][loop_col]);
      value_set_si (cstr->p[row][loop_col], -1);

      nb_iters = instantiate_parameters (SCOP_ENTRY (scop)->loop_father,
					 nb_iters);
      value_init (one);
      value_set_si (one, 1);
      scan_tree_for_params (scop, nb_iters, cstr, row, loop_col, one);
      value_clear (one);
    }

  res->domain = cloog_domain_matrix2domain (cstr);

  tmp->loop_num = loop->num;
  slot = htab_find_slot (SCOP_LOOP2CLOOG_LOOP (scop), tmp, INSERT);
  if (!*slot)
    *slot = new_loop_to_cloog_loop_str (loop->num, loop_col - 1, res);

  /* Now set up the other loop constructs.  CLooG is expecting to see
     a list of loops chained with the res->next pointer.  Don't use
     res->inner for representing inner loops: this information is
     contained in the scattering matrix.  */
  if (loop->inner && loop_in_scop_p (loop->inner, scop))
    res->next = setup_cloog_loop (scop, loop->inner, cstr, nb_outer_loops + 1);

  if (loop->next && loop_in_scop_p (loop->next, scop))
    {
      CloogLoop *l = res;

      /* Append at the end of the res->next list.  */
      while (l->next)
	l = l->next;
      l->next = setup_cloog_loop (scop, loop->next, outer_cstr, nb_outer_loops);
    }

  {
    static int number = 0;
    statement = cloog_statement_alloc (number++);
  }

  res->block = cloog_block_alloc (statement, NULL, 0, NULL, nb_outer_loops + 1);
  
  return res;
}

/* Converts the graphite scheduling function into a cloog scattering
   function matrix, which restores the original control flow.  */

static CloogMatrix *
schedule_to_scattering (graphite_bb_p gb) 
{
  /* Conservative aproximation, the maximal loop depth of all bbs would be
     sufficient, as we use in cloog one iterator for loops of the same loop
     depth.  */
  int max_nb_iterators = scop_nb_loops (GBB_SCOP (gb));
  struct loop *loop = gbb_loop (gb);
  int nb_iterators = nb_loops_around_gb (gb);

  /* Number of columns:
     1                        col  = Eq/Inq,
     2 * max_nb_iterators + 1 cols = Scattering dimensions,
     nb_iterators             cols = bb's iterators,
     1                        col  = Constant 1
   The scattering domain contains one dimension for every iterator (which 
   iteration of this loop should be scattered) and max_nb_iterators + 1
   dimension for the textual order of every loop.  */ 
  int nb_cols = 1 + 2 * max_nb_iterators + 1 + nb_iterators + 1;
  int col_const = nb_cols - 1; 
  int col_iter_offset = 1 + 2 * max_nb_iterators + 1;

  CloogMatrix *scat = cloog_matrix_alloc (nb_iterators * 2 + 1, nb_cols);

  int i;
  int loop_index;
  int row = 0; 

  /* Reverse, because we get the inner loops first.  */
  for (i = nb_iterators - 1; i >= 0; i--) 
    {
      loop_index = scop_loop_index (GBB_SCOP (gb), loop);

      /* Set textual order for bb's of loop.  */
      value_init (scat->p[row][2 * i + 3]);
      value_set_si (scat->p[row][2 * i + 3], 1);
      value_init (scat->p[row][col_const]);
      value_set_si (scat->p[row][col_const],
		    GBB_STATIC_SCHEDULE (gb)[loop_index]);

      row++;

      /* Set scattering for loop iterator.  */
      value_init (scat->p[row][2 * i + 2]);
      value_set_si (scat->p[row][2 * i + 2], 1);
      value_init (scat->p[row][col_iter_offset + i]);
      value_set_si (scat->p[row][col_iter_offset + i], 1);

      loop = loop_outer (loop);
      row++;
    }

  /* Set textual order for outer loop.  */
#if 0
  loop_index = scop_loop_index (GBB_SCOP (gb), loop);

  value_init (scat->p[row][1]);
  value_set_si (scat->p[row][1], 1);
  value_init (scat->p[row][col_const]);
  value_set_si (scat->p[row][col_const], GBB_STATIC_SCHEDULE (gb)[loop_index]);
#endif

 return scat; 
}

/* Build the current domain matrix: the loops belonging to the current
   SCOP, and that vary for the execution of the current basic block.
   Returns false if there is no loop in SCOP.  */

static bool
build_scop_iteration_domain (scop_p scop)
{
  struct loop *loop = first_loop_in_scop (scop);
  CloogMatrix *outer_cstr;

  if (loop == NULL)
    return false;

  /* The outermost constraints is a matrix that has:
     - first column: eq/ineq boolean
     - last column: a constant
     - nb_params_in_scop columns for the parameters used in the scop.  */
  outer_cstr = cloog_matrix_alloc (0, 2 + nb_params_in_scop (scop));
  SCOP_PROG (scop)->loop = setup_cloog_loop (scop, loop, outer_cstr, 0);
  return true;
}

/* Returns the dimension of the iteration domain of LOOP_NUM with
   respect to SCOP.  */

static int
loop_domain_dim (unsigned int loop_num, scop_p scop)
{
  struct loop_to_cloog_loop_str tmp, *slot; 

  tmp.loop_num = loop_num;
  slot = (struct loop_to_cloog_loop_str *) htab_find (SCOP_LOOP2CLOOG_LOOP (scop), &tmp);

  /* The loop containing the entry of the scop is not always part of
     the scop, and it is not registered in SCOP_LOOP2CLOOG_LOOP.  */
  if (!slot)
    return nb_params_in_scop (scop) + 2;

  return slot->cloog_loop->domain->polyhedron->Dimension + 2;
}

/* Returns the number of loops around data reference REF.  */

static int
ref_nb_loops (data_reference_p ref)
{
  return loop_domain_dim (loop_containing_stmt (DR_STMT (ref))->num, DR_SCOP (ref));
}

/* Returns the dimension of an iteration vector in LOOP_NUM with
   respect to SCOP.  */

static int
loop_iteration_vector_dim (unsigned int loop_num, scop_p scop)
{
  return loop_domain_dim (loop_num, scop) - 2 - nb_params_in_scop (scop);
}

/* Initializes an equation CY of the access matrix using the
   information for a subscript from ACCESS_FUN, relatively to the loop
   indexes from LOOP_NEST and parameter indexes from PARAMS.  NDIM is
   the dimension of the array access, i.e. the number of
   subscripts.  Returns true when the operation succeeds.  */

static bool
build_access_matrix_with_af (tree access_fun, lambda_vector cy,
			     scop_p scop, unsigned ndim)
{
  switch (TREE_CODE (access_fun))
    {
    case POLYNOMIAL_CHREC:
      {
	tree left = CHREC_LEFT (access_fun);
	tree right = CHREC_RIGHT (access_fun);
	unsigned var;

	if (TREE_CODE (right) != INTEGER_CST)
	  return false;
        
	var = loop_iteration_vector_dim (CHREC_VARIABLE (access_fun), scop);
	cy[var] = int_cst_value (right);

	switch (TREE_CODE (left))
	  {
	  case POLYNOMIAL_CHREC:
	    return build_access_matrix_with_af (left, cy, scop, ndim);

	  case INTEGER_CST:
	    /* Constant part.  */
	    cy[ndim - 1] = int_cst_value (left);
	    return true;

	  default:
	    /* TODO: also consider that access_fn can have parameters.  */
	    return false;
	  }
      }
    case INTEGER_CST:
      /* Constant part.  */
      cy[ndim - 1] = int_cst_value (access_fun);
      return true;

    default:
      return false;
    }
}

/* Initialize the access matrix in the data reference REF with respect
   to the loop nesting LOOP_NEST.  Return true when the operation
   succeeded.  */

static bool
build_access_matrix (data_reference_p ref, graphite_bb_p gb)
{
  unsigned i, ndim = DR_NUM_DIMENSIONS (ref);

  DR_SCOP (ref) = GBB_SCOP (gb);
  DR_ACCESS_MATRIX (ref) = VEC_alloc (lambda_vector, heap, ndim);

  for (i = 0; i < ndim; i++)
    {
      lambda_vector v = lambda_vector_new (ref_nb_loops (ref));
      scop_p scop = GBB_SCOP (gb);
      tree af = DR_ACCESS_FN (ref, i);

      if (!build_access_matrix_with_af (af, v, scop, ref_nb_loops (ref)))
	return false;

      VEC_safe_push (lambda_vector, heap, DR_ACCESS_MATRIX (ref), v);
    }

  return true;
}

/* Build a schedule for each basic-block in the SCOP.  */

static void
build_scop_data_accesses (scop_p scop)
{
  unsigned i;
  graphite_bb_p gb;

  for (i = 0; VEC_iterate (graphite_bb_p, SCOP_BBS (scop), i, gb); i++)
    {
      unsigned j;
      block_stmt_iterator bsi;
      data_reference_p dr;
      struct loop *nest = outermost_loop_in_scop (scop, GBB_BB (gb));

      /* On each statement of the basic block, gather all the occurences
	 to read/write memory.  */
      GBB_DATA_REFS (gb) = VEC_alloc (data_reference_p, heap, 5);
      for (bsi = bsi_start (GBB_BB (gb)); !bsi_end_p (bsi); bsi_next (&bsi))
	find_data_references_in_stmt (nest, bsi_stmt (bsi),
				      &GBB_DATA_REFS (gb));

      /* Construct the access matrix for each data ref, with respect to
	 the loop nest of the current BB in the considered SCOP.  */
      for (j = 0; VEC_iterate (data_reference_p, GBB_DATA_REFS (gb), j, dr); j++)
	build_access_matrix (dr, gb);
    }
}

/* Find the right transform for the SCOP, and return a Cloog AST
   representing the new form of the program.  */

static struct clast_stmt *
find_transform (scop_p scop)
{
  CloogOptions *options = cloog_options_malloc ();
  CloogProgram *prog;
  struct clast_stmt *stmt;

  /* Change cloog output language to C.  If we do use FORTRAN instead, cloog
     will stop e.g. with "ERROR: unbounded loops not allowed in FORTRAN.", if
     we pass an incomplete program to cloog.  */
  options->language = LANGUAGE_C;

  /* Print the program we insert into cloog. */
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Cloog Input [\n");
      cloog_program_print (dump_file, SCOP_PROG(scop));
      fprintf (dump_file, "]\n");
    }

  prog = cloog_program_generate (SCOP_PROG (scop), options);
  stmt = cloog_clast_create (prog, options);

  /* Print the program we get from cloog. */
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Cloog Output[\n");
      pprint (dump_file, stmt, 0, options);
      cloog_program_dump_cloog (dump_file, prog);
      fprintf (dump_file, "]\n");
    }

  return stmt;
}

/* GIMPLE Loop Generator: generates loops from STMT in GIMPLE form for
   the given SCOP.  */

static void
gloog (scop_p scop ATTRIBUTE_UNUSED, struct clast_stmt *stmt)
{
  cloog_clast_free (stmt);
}

/* Returns a matrix representing the data dependence between memory
   accesses A and B in the context of SCOP.  */

static CloogMatrix *
initialize_dependence_polyhedron (scop_p scop, 
                                  struct data_reference *a, 
                                  struct data_reference *b)
{
  unsigned nb_cols, nb_rows, nb_loops, nb_params, nb_iter1, nb_iter2;
  struct loop_to_cloog_loop_str tmp, *slot1, *slot2; 
  unsigned row, col;
  CloogMatrix *domain1, *domain2;
  CloogMatrix *dep_constraints;
  lambda_vector access_row_vector;
  struct loop *containing_loop;
  containing_loop = loop_containing_stmt (DR_STMT (a));
  tmp.loop_num = containing_loop->num;
  slot1 = (struct loop_to_cloog_loop_str *) htab_find (SCOP_LOOP2CLOOG_LOOP(scop), &tmp); 
          
  containing_loop = loop_containing_stmt (DR_STMT (b));
  tmp.loop_num = containing_loop->num;
  slot2 = (struct loop_to_cloog_loop_str *) htab_find (SCOP_LOOP2CLOOG_LOOP(scop), &tmp); 
  /* TODO: insert checking for possible null values of slot1 and
     slot2.  */

  domain1 = cloog_domain_domain2matrix (slot1->cloog_loop->domain);
  domain2 = cloog_domain_domain2matrix (slot2->cloog_loop->domain);

  nb_params = scop_nb_params (scop);
  nb_iter1 = domain1->NbColumns - 2 - nb_params;
  nb_iter2 = domain2->NbColumns - 2 - nb_params;
  nb_loops = scop_nb_loops (scop);

  nb_cols = nb_iter1 + nb_iter2 + scop_nb_params (scop) + 2;
  nb_rows = domain1->NbRows + domain2->NbRows + DR_NUM_DIMENSIONS (a);
  dep_constraints = cloog_matrix_alloc (nb_rows, nb_cols);

  /* Initialize dependence polyhedron.  TODO: do we need it?  */
  for (row = 0; row < dep_constraints->NbRows ; row++)
    for (col = 0; col < dep_constraints->NbColumns; col++)
      value_init (dep_constraints->p[row][col]);

  /* Copy the iterator part of Ds (domain of S statement), with eq/neq
     column.  */
  for (row = 0; row < domain1->NbRows; row++)
    for (col = 0; col <= nb_iter1; col++)
      value_assign (dep_constraints->p[row][col], domain1->p[row][col]);

  /* Copy the parametric and constant part of Ds.  */
  for (row = 0; row < domain1->NbRows; row++)
    {
      value_assign (dep_constraints->p[row][nb_cols-1],
		    domain1->p[row][domain1->NbColumns - 1]);
      for (col = 1; col <= nb_params; col++)
	value_assign (dep_constraints->p[row][col + nb_iter1 + nb_iter2],
		      domain1->p[row][col + nb_iter1]);
    }

  /* Copy the iterator part of Dt (domain of T statement), without eq/neq column.  */
  for (row = 0; row < domain2->NbRows; row++)
    for (col = 1; col <= nb_iter2; col++)
      value_assign (dep_constraints->p[row + domain1->NbRows][col + nb_iter2],
		    domain2->p[row][col]);
  
  /* Copy the eq/neq column of Dt to dependence polyhedron.  */
  for (row = 0; row < domain2->NbRows; row++)
    value_assign (dep_constraints->p[row + domain1->NbRows][0], domain2->p[row][0]);

  /* Copy the parametric and constant part of Dt.  */
  for (row = 0; row < domain2->NbRows; row++)
    {
      value_assign (dep_constraints->p[row + domain1->NbRows][nb_cols-1],
		    domain1->p[row][domain2->NbColumns - 1]);
      for (col = 1; col <= nb_params; col++)
        value_assign (dep_constraints->p[row + domain1->NbRows][col + nb_iter1 + nb_iter2],
                      domain2->p[row][col + nb_iter2]);
    }

  /* Copy Ds access matrix.  */
  for (row = 0; VEC_iterate (lambda_vector, DR_ACCESS_MATRIX (a), row, access_row_vector); row++)
    {
      for (col = 1; col <= nb_iter1; col++)
	value_set_si (dep_constraints->p[row + domain1->NbRows + domain2->NbRows][col],
		      access_row_vector[col]);              

      value_set_si (dep_constraints->p[row + domain1->NbRows + domain2->NbRows][nb_cols-1], 
                    access_row_vector[ref_nb_loops (a) - 1]);
      /* TODO: do not forget about parametric part.  */
    }

  /* Copy -Dt access matrix.  */
  for (row = 0; VEC_iterate (lambda_vector, DR_ACCESS_MATRIX (b), row, access_row_vector); row++)
    {
      for (col = 1; col <= nb_iter2; col++)
	value_set_si (dep_constraints->p[row + domain1->NbRows + domain2->NbRows][nb_iter1 + col], 
		      -access_row_vector[col]);              

      value_sub_int (dep_constraints->p[row + domain1->NbRows + domain2->NbRows][nb_cols-1],
                     dep_constraints->p[row + domain1->NbRows + domain2->NbRows][nb_cols-1],
                     access_row_vector[ref_nb_loops (b) - 1]);
    }
         
  return dep_constraints;
}

/* Returns a new dependence polyhedron for data references A and B.  */

static struct data_dependence_polyhedron *
initialize_data_dependence_polyhedron (bool loop_carried,
                                       CloogDomain *domain,
                                       unsigned level,
                                       struct data_reference *a,
                                       struct data_reference *b)
{
  struct data_dependence_polyhedron *res;

  res = XNEW (struct data_dependence_polyhedron);
  res -> a = a;
  res -> b = b;
  res -> loop_carried = loop_carried;
  res -> level = level;

  if (loop_carried)
    res -> polyhedron = domain; 
  else
    res -> polyhedron = NULL;

  return res;
}

/* Returns true when the last row of DOMAIN polyhedron is zero.  */

static bool 
is_empty_polyhedron (CloogDomain *domain)
{
  Polyhedron *polyhedron;
  unsigned i, last_column, last_row;
  polyhedron = domain->polyhedron;
  last_column = polyhedron->Dimension + 2;
  last_row = polyhedron->NbConstraints - 1;

  for  (i = 1; i < last_column - 1; i++)
    if (!value_zero_p (polyhedron->Constraint[last_row][i]))
      return false;

  return !value_zero_p (polyhedron->Constraint[last_row][last_column - 1]);
}

/* Returns true if statement A, contained in basic block GB_A,
   precedes statement B, contained in basic block GB_B.  The decision
   is based on static schedule of basic block's and relative position
   of statements.  */

static bool 
statement_precedes_p (scop_p scop,
                      graphite_bb_p gb_a,
                      tree a,
                      graphite_bb_p gb_b,
                      tree b,
                      unsigned p)
{
  block_stmt_iterator bsi;
  bool statm_a_found, statm_b_found;
  struct loop_to_cloog_loop_str tmp, *slot; 

  if (GBB_STATIC_SCHEDULE (gb_a)[p - 1] < GBB_STATIC_SCHEDULE (gb_b)[p - 1])
    return true;

  else if (GBB_STATIC_SCHEDULE (gb_a)[p - 1] == GBB_STATIC_SCHEDULE (gb_b)[p - 1])
    {
      statm_a_found = false;
      statm_b_found = false;
      /* TODO: You can use stmt_ann->uid for a slight speedup.  */
      /* If static schedules are the same -> gb1 = gb2.  */
      /* GBB_BB (gb_a)->loop_father; */
      tmp.loop_num = GBB_BB (gb_a)->loop_father->num;
      slot = (struct loop_to_cloog_loop_str *) htab_find (SCOP_LOOP2CLOOG_LOOP(scop), &tmp);

      if (slot->loop_position == p - 1)
	for (bsi = bsi_start (GBB_BB (gb_a)); !bsi_end_p (bsi); bsi_next (&bsi))
	  {
	    if (bsi_stmt (bsi) == a)
	      statm_a_found = true;
        
	    if (statm_a_found && bsi_stmt (bsi) == b)
	      return true;
	  }
    }

  return false;
}

/* Returns the polyhedral data dependence graph for SCOP.  */

static struct graph *
build_rdg_all_levels (scop_p scop)
{
  unsigned i, j, row, nb_loops;
  unsigned i1, j1;
  int va, vb;
  signed p;
  graphite_bb_p gb1, gb2;
  struct graph * rdg = NULL;
  struct data_reference *a, *b;
  CloogMatrix *dep_constraints, *temp_matrix;
  CloogDomain *simplified;
  block_stmt_iterator bsi;
  struct graph_edge *e;
  
 /* VEC (data_reference_p, heap) *datarefs;*/
 /* All the statements that are involved in dependences are stored in
    this vector.  */
  VEC (tree, heap) *stmts = VEC_alloc (tree, heap, 10);
  VEC (ddp_p, heap) *ddps = VEC_alloc (ddp_p, heap, 10); 
  ddp_p ddp;    
  /* datarefs = VEC_alloc (data_reference_p, heap, 2);*/
  nb_loops = scop_nb_loops (scop); 
  for (i = 0; VEC_iterate (graphite_bb_p, SCOP_BBS (scop), i, gb1); i++)
    {
      for (bsi = bsi_start (GBB_BB (gb1)); !bsi_end_p (bsi); bsi_next (&bsi))
	VEC_safe_push (tree, heap, stmts, bsi_stmt (bsi));

      for (i1 = 0; VEC_iterate (data_reference_p, GBB_DATA_REFS (gb1), i1, a); i1++)
	for (j = 0; VEC_iterate (graphite_bb_p, SCOP_BBS (scop), j, gb2); j++)
	  for (j1 = 0; VEC_iterate (data_reference_p, GBB_DATA_REFS (gb2), j1, b); j1++)
	    if ((!DR_IS_READ (a) || !DR_IS_READ (b)) && dr_may_alias_p (a,b)
		&& operand_equal_p (DR_BASE_OBJECT (a), DR_BASE_OBJECT (b), 0))
	      /* TODO: the previous check might be too restrictive.  */
	      for (i = 1; i <= 2 * nb_loops + 1; i++)
		{
		  /* S - gb1 */
		  /* T - gb2 */
		  /* S -> T, T - S >=1 */
		  /* p is alternating sequence 0,1,-1,2,-2,... */
		  p = (i / 2) * (1 - (i % 2)*2);
		  if (p == 0)
		    {
		      dep_constraints = initialize_dependence_polyhedron (scop, a, b);
		      temp_matrix = AddANullRow (dep_constraints);
		    }
		  else if (p > 0)
		    {
		      /* assert B0, B1, ..., Bp-1 satisfy the equality */
                
		      temp_matrix = AddANullRow (temp_matrix);
                
		      row = temp_matrix->NbRows - 1; 
                
		      value_set_si (temp_matrix->p[row][0], 1);
		      value_set_si (temp_matrix->p[row][p], -GBB_ALPHA (gb1)[p - 1]);
		      value_set_si (temp_matrix->p[row][p + scop_nb_loops (scop)], GBB_ALPHA (gb2)[p - 1]);
		      value_set_si (temp_matrix->p[row][temp_matrix->NbColumns - 1], -1);

		      simplified = cloog_domain_matrix2domain (temp_matrix);
		      temp_matrix = RemoveRow (temp_matrix, temp_matrix->NbRows - 1);

		      if (!is_empty_polyhedron (simplified))
			{
			  ddp = initialize_data_dependence_polyhedron (true, simplified, p, a, b);
			  VEC_safe_push (ddp_p, heap, ddps, ddp);
			  break;
			}
		    }
		  else if (p < 0)
		    {
                
		      /* TODO: do not forget about memory leaks,
			 temp_matrix is a new matrix!  */
		      temp_matrix = AddANullRow (temp_matrix);
                
		      row = temp_matrix->NbRows - 1; 
		      value_set_si (temp_matrix->p[row][-p], -GBB_ALPHA (gb1)[-p - 1]);
		      value_set_si (temp_matrix->p[row][-p + scop_nb_loops (scop)], GBB_ALPHA (gb2)[-p - 1]);

		      simplified = cloog_domain_matrix2domain (temp_matrix);
                
		      if (statement_precedes_p (scop, gb1, DR_STMT (a), gb2, DR_STMT (b), -p))
			{
			  ddp = initialize_data_dependence_polyhedron (false, simplified, -p, a, b);
			  VEC_safe_push (ddp_p, heap, ddps, ddp);
			  break;
			}
		    }
		}    
    }

  rdg = build_empty_rdg (VEC_length (tree, stmts));
  create_rdg_vertices (rdg, stmts);

  for (i = 0; VEC_iterate (ddp_p, ddps, i, ddp); i++)
    {
      va = rdg_vertex_for_stmt (rdg, DR_STMT (ddp->a)); 
      vb = rdg_vertex_for_stmt (rdg, DR_STMT (ddp->b));
      e = add_edge (rdg, va, vb);
      e->data = ddp;
    }

  VEC_free (tree, heap, stmts);
  return rdg;  
}

/* Dumps the dependence graph G to file F.  */

static void
dump_dependence_graph (FILE *f, struct graph *g)
{
  int i;
  struct graph_edge *e;

  for (i = 0; i < g->n_vertices; i++)
    {
      if (!g->vertices[i].pred
	  && !g->vertices[i].succ)
	continue;

      fprintf (f, "vertex: %d (%d)\nStatement: ", i, g->vertices[i].component);
      print_generic_expr (f, RDGV_STMT (&(g->vertices[i])), 0);
      fprintf (f, "\n-----------------\n");
      
      for (e = g->vertices[i].pred; e; e = e->pred_next)
        {
          fprintf (f, "edge %d -> %d\n", e->src, i);
          struct data_dependence_polyhedron *ddp = RDGE_DDP (e);
          if (ddp->polyhedron != NULL)
            {
              cloog_domain_print (f, ddp->polyhedron); 
            }
          fprintf (f, "-----------------\n");
        }

      for (e = g->vertices[i].succ; e; e = e->succ_next)
        {
          fprintf (f, "edge %d -> %d\n", i, e->dest);
          struct data_dependence_polyhedron *ddp = RDGE_DDP (e);
          if (ddp->polyhedron != NULL)
            {
              cloog_domain_print (f, ddp->polyhedron); 
            }
          fprintf (f, "-----------------\n");
        }
      fprintf (f, "\n");
    }
}

/* Perform a set of linear transforms on LOOPS.  */

void
graphite_transform_loops (void)
{
  unsigned i;
  scop_p scop;
  struct graph * rdg = NULL;

  current_scops = VEC_alloc (scop_p, heap, 3);

  calculate_dominance_info (CDI_DOMINATORS);
  calculate_dominance_info (CDI_POST_DOMINATORS);

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "Graphite loop transformations \n");

  build_scops ();

  for (i = 0; VEC_iterate (scop_p, current_scops, i, scop); i++)
    {
      build_scop_bbs (scop);
      build_scop_loop_nests (scop);
      build_scop_canonical_schedules (scop);
      find_scop_parameters (scop);
      build_scop_context (scop);

      if (!build_scop_iteration_domain (scop))
	continue;

      build_scop_data_accesses (scop);

      if (0)
	build_rdg_all_levels (scop);

      gloog (scop, find_transform (scop));
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      dot_all_scops_1 (dump_file);
      print_scops (dump_file, 2);
      fprintf (dump_file, "\nnumber of SCoPs: %d\n",
	       VEC_length (scop_p, current_scops));
      if (0)
	dump_dependence_graph (dump_file, rdg);
    }

  free_scops (current_scops);
}
