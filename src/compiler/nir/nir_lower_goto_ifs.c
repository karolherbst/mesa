#include "nir.h"
#include "nir_builder.h"

struct lower_state {
   struct set *visited;
};

typedef enum block_type {
   BLOCK_TYPE_BLOCK,
   BLOCK_TYPE_IF,
   BLOCK_TYPE_LOOP,
} block_type;

static nir_block *
follow_until_multiple_preds(nir_block *block)
{
   /* follow blocks with one sucessor until we hit one with multiple
    * predecessors */
   while (block) {
      /* if a successor is deper in the CF tree, skip to the end of that
       * node */
      switch (block->cf_node.parent->type) {
      case nir_cf_node_if: {
         nir_if *nif = nir_cf_node_as_if(block->cf_node.parent);
         block = nir_if_last_then_block(nif)->successors[0];
         break;
      }
      case nir_cf_node_loop: {
         nir_loop *loop = nir_cf_node_as_loop(block->cf_node.parent);
         block = nir_cf_node_as_block(nir_cf_node_next(&loop->cf_node));
         break;
      }
      case nir_cf_node_function:
         /* if we found a block with multiple predecessors, at least one
          * of those have to be on the top level of a function */
         if (block->predecessors->entries > 1) {
            set_foreach(block->predecessors, entry) {
               const nir_block *b = entry->key;
               if (b->cf_node.parent->type == nir_cf_node_function)
                  return block;
            }
         }
         /* if we have two successors, we give up if they have different
          * parents or the common parent is a function */
         if (block->successors[1]) {
            if (block->successors[0]->cf_node.parent !=
                block->successors[1]->cf_node.parent)
               return NULL;
            if (block->successors[0]->cf_node.parent->type == nir_cf_node_function)
               return NULL;
         }
         block = block->successors[0];
         break;
      default:
         unreachable("unhandled CF type");
      }
   }

   return block;
}

/* for the given block, what type of cf node can we create out of it.
 * BLOCK_TYPE_BLOCK means we can't figure out anything meaningful */
static block_type
check_block(nir_block *block)
{
   assert(block);
   if (!block->successors[1])
      return BLOCK_TYPE_BLOCK;

   /* the main trick here is to follow each paths until we hit a block with
    * multiple predecessors and see how those blocks relate to each other */
   nir_block *t = follow_until_multiple_preds(block->successors[0]);
   nir_block *e = follow_until_multiple_preds(block->successors[1]);

   if (!e && !t)
      return BLOCK_TYPE_BLOCK;

   /* if we follow both successors and the first block with multiple
    * predecessors is the same on both paths, we can create an if node */
   if (e == t)
      return BLOCK_TYPE_IF;

   /* if one of the paths followed ends up with the initial block, we can
    * create a loop node */
   if (e == block || t == block) {
      /* if both paths end up in the same block we are completly screwed */
      assert(e != t);
      return BLOCK_TYPE_LOOP;
   }

   return BLOCK_TYPE_BLOCK;
}

static inline void
unlink_blocks(nir_block *pred, nir_block *succ)
{
   if (!succ)
      return;
   if (!_mesa_set_search(succ->predecessors, pred))
      return;
   _mesa_set_remove_key(succ->predecessors, pred);
   if (pred->successors[0] == succ)
      pred->successors[0] = NULL;
   if (pred->successors[1] == succ)
      pred->successors[1] = NULL;
}

static inline void
link_blocks(nir_block *pred, nir_block *succ0, nir_block *succ1)
{
   if (succ0) {
      unlink_blocks(pred, pred->successors[0]);
      _mesa_set_add(succ0->predecessors, pred);
      pred->successors[0] = succ0;
   }
   if (succ1) {
      unlink_blocks(pred, pred->successors[1]);
      _mesa_set_add(succ1->predecessors, pred);
      pred->successors[1] = succ1;
   }
}

static void
handle_if(nir_builder *b, nir_if *nif, nir_block *block, nir_block *head, nir_block *merge, bool then)
{
   nir_block *nif_end = nir_if_last_then_block(nif)->successors[0];
   if (block == merge) {
      unlink_blocks(head, merge);
      block = then ? nir_if_first_then_block(nif) : nir_if_first_else_block(nif);
      if (then)
         link_blocks(head, block, NULL);
      else
         link_blocks(head, NULL, block);
   } else {
      nir_block *it = then ? nir_if_first_then_block(nif) : nir_if_first_else_block(nif);

      exec_node_remove(&block->cf_node.node);
      exec_list_push_tail(then ? &nif->then_list : &nif->else_list, &block->cf_node.node);
      block->cf_node.parent = &nif->cf_node;

      unlink_blocks(head, block);
      unlink_blocks(it, nif_end);
      unlink_blocks(block, merge);

      if (then)
         link_blocks(head, it, NULL);
      else
         link_blocks(head, NULL, it);
      link_blocks(it, block, NULL);

      if (!block->successors[0]) {
         link_blocks(block, nif_end, NULL);
         return;
      }
      block = block->successors[0];
      nir_block *prev = NULL;
      while (block != merge) {
         switch (block->cf_node.parent->type) {
         case nir_cf_node_function: {
            assert(block->successors[0]);
            assert(!block->successors[1]);

            exec_node_remove(&block->cf_node.node);
            exec_list_push_tail(then ? &nif->then_list : &nif->else_list, &block->cf_node.node);
            block->cf_node.parent = &nif->cf_node;

            prev = block;
            block = block->successors[0];
            break;
         }

         case nir_cf_node_if: {
            nir_if *dnif = nir_cf_node_as_if(block->cf_node.parent);
            block = nir_cf_node_as_block(nir_cf_node_next(&dnif->cf_node));

            unlink_blocks(nif_end, nir_if_first_then_block(dnif));
            unlink_blocks(nif_end, nir_if_first_else_block(dnif));

            exec_node_remove(&dnif->cf_node.node);
            exec_list_push_tail(then ? &nif->then_list : &nif->else_list, &dnif->cf_node.node);
            dnif->cf_node.parent = &nif->cf_node;

            /* if the block after the nif isn't the merge, we can move it up
             * instead of creating a new block */
            nir_block *after;
            if (block != merge) {
               after = block;
               block = after->successors[0];
               assert(!after->successors[1]);
               exec_node_remove(&after->cf_node.node);
            } else {
               after = nir_block_create(b->shader);

               /* fix then and else blocks */
               nir_block *e = nir_if_last_else_block(dnif);
               nir_block *t = nir_if_last_then_block(dnif);
               if (e)
                  link_blocks(e, after, NULL);
               if (t)
                  link_blocks(t, after, NULL);

               link_blocks(after, block, NULL);
            }

            exec_list_push_tail(then ? &nif->then_list : &nif->else_list, &after->cf_node.node);
            after->cf_node.parent = &nif->cf_node;
            prev = after;
            break;
         }

         case nir_cf_node_loop: {
            nir_loop *loop = nir_cf_node_as_loop(block->cf_node.parent);
            prev = block;
            block = nir_cf_node_as_block(nir_cf_node_next(&loop->cf_node));

            exec_node_remove(&loop->cf_node.node);
            exec_list_push_tail(then ? &nif->then_list : &nif->else_list, &loop->cf_node.node);
            loop->cf_node.parent = &nif->cf_node;

            nir_block *after = nir_block_create(b->shader);
            exec_list_push_tail(then ? &nif->then_list : &nif->else_list, &after->cf_node.node);
            after->cf_node.parent = &nif->cf_node;

            /* fix all break blocks */
            set_foreach(merge->predecessors, entry) {
               nir_block *break_block = (nir_block*)entry->key;
               unlink_blocks(break_block, merge);
               link_blocks(break_block, after, NULL);
            }
            link_blocks(after, nif_end, NULL);
            prev = after;
            break;
         }
         default:
            unreachable("unkown CF node type");
         }
      }
      link_blocks(prev, nif_end, NULL);
      link_blocks(nif_end, merge, NULL);
   }
}

static bool
lower_block(nir_builder *b, nir_block *block)
{
   block_type type = check_block(block);
   if (type == BLOCK_TYPE_BLOCK)
      return false;

   nir_block *head = block;
   nir_instr *instr = nir_block_last_instr(head);
   assert(instr->type == nir_instr_type_jump);
   nir_jump_instr *jump = nir_instr_as_jump(instr);
   assert(jump->type == nir_jump_goto_if);

   nir_block *t = head->successors[0];
   nir_block *e = head->successors[1];

   switch (type) {
   case BLOCK_TYPE_IF: {
      nir_block *merge = follow_until_multiple_preds(e);

      assert(merge->predecessors->entries >= 2);
      b->cursor = nir_after_block(head);

      nir_if *nif = nir_push_if_src(b, jump->condition);
      nir_block *nif_end = nir_if_last_then_block(nif)->successors[0];
      nir_instr_remove(instr);
      handle_if(b, nif, t, head, merge, true);
      nir_push_else(b, nif);
      handle_if(b, nif, e, head, merge, false);
      nir_pop_if(b, nif);
      link_blocks(nif_end, merge, NULL);

      break;
   }
   case BLOCK_TYPE_LOOP: {
      nir_block *after;
      nir_block *head;
      if (e == block) {
         after = t;
         head = e;
      } else if (t == block) {
         after = e;
         head = t;
      } else {
         assert(false);
      }

      /* TODO: handle loop heads with multiple predecessors */
      assert(head->predecessors->entries == 2);
      b->cursor = nir_before_block(head);

      nir_loop *loop = nir_loop_create_empty(b->shader);
      loop->cf_node.parent = head->cf_node.parent;
      exec_node_insert_node_before(&head->cf_node.node, &loop->cf_node.node);

      exec_node_remove(&head->cf_node.node);
      exec_list_push_tail(&loop->body, &head->cf_node.node);
      head->cf_node.parent = &loop->cf_node;

      b->cursor = nir_after_block(head);

      nir_if *nif = nir_push_if_src(b, jump->condition);
      nir_instr_remove(instr);
      if (t == after)
         nir_jump(b, nir_jump_break);
      if (t == head)
         nir_jump(b, nir_jump_continue);
      nir_push_else(b, nif);
      if (e == after)
         nir_jump(b, nir_jump_break);
      if (t == after)
         nir_jump(b, nir_jump_continue);
      nir_pop_if(b, nif);
      break;
   }
   case BLOCK_TYPE_BLOCK:
      break;
   }

   return true;
}

static bool
lower_impl(nir_function_impl *impl)
{
   nir_builder b;
   nir_builder_init(&b, impl);

   bool final_progress = false;
   bool progress;

   do {
      progress =  false;
      for (nir_cf_node *node = &nir_start_block(impl)->cf_node;
           node; node = nir_cf_node_next(node)) {
         if (node->type != nir_cf_node_block)
            continue;
         progress |= lower_block(&b, nir_cf_node_as_block(node));
         final_progress |= progress;
      }
   } while (progress);

   return progress;
}

bool
nir_lower_goto_ifs(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= lower_impl(function->impl);
   }

   return progress;
}
