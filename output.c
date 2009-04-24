
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "output.h"
#include "allegrex.h"
#include "utils.h"

static
void get_base_name (char *filename, char *basename, size_t len)
{
  char *temp;

  temp = strrchr (filename, '/');
  if (temp) filename = &temp[1];

  strncpy (basename, filename, len - 1);
  basename[len - 1] = '\0';
  temp = strchr (basename, '.');
  if (temp) *temp = '\0';
}


static
void print_header (FILE *out, struct code *c, char *headerfilename)
{
  uint32 i, j;
  char buffer[256];
  int pos = 0;

  while (pos < sizeof (buffer) - 1) {
    char c = headerfilename[pos];
    if (!c) break;
    if (c == '.') c = '_';
    else c = toupper (c);
    buffer[pos++] =  c;
  }
  buffer[pos] = '\0';

  fprintf (out, "#ifndef __%s\n", buffer);
  fprintf (out, "#define __%s\n\n", buffer);

  for (i = 0; i < c->file->modinfo->numexports; i++) {
    struct prx_export *exp = &c->file->modinfo->exports[i];

    fprintf (out, "/*\n * Exports from library: %s\n */\n", exp->name);
    for (j = 0; j < exp->nfuncs; j++) {
      struct prx_function *func = &exp->funcs[j];
      if (func->name) {
        fprintf (out, "void %s (void);\n",func->name);
      } else {
        fprintf (out, "void %s_%08X (void);\n", exp->name, func->nid);
      }
    }
    fprintf (out, "\n");
  }

  fprintf (out, "#endif /* __%s */\n", buffer);
}

static
void print_subroutine_name (FILE *out, struct subroutine *sub)
{
  if (sub->export) {
    if (sub->export->name) {
      fprintf (out, "%s", sub->export->name);
    } else {
      fprintf (out, "nid_%08X", sub->export->nid);
    }
  } else {
    fprintf (out, "sub_%05X", sub->begin->address);
  }
}

static
void print_value (FILE *out, struct value *val)
{
  switch (val->type) {
  case VAL_CONSTANT: fprintf (out, "0x%08X", val->value); break;
  case VAL_REGISTER: fprintf (out, "%s", gpr_names[val->value]); break;
  default:
    fprintf (out, "UNK");
  }
}

static
void ident_line (FILE *out, int size)
{
  int i;
  for (i = 0; i < size; i++)
    fprintf (out, "  ");
}

static
void print_block (FILE *out, int ident, struct basicblock *block)
{
  element el, vel;
  struct location *loc;

  if (block->type != BLOCK_SIMPLE) return;
  el = list_head (block->operations);
  while (el) {
    struct operation *op = element_getvalue (el);

    if (op->type == OP_ASM) {
      ident_line (out, ident);
      fprintf (out, "__asm__ (\n");
      for (loc = op->begin; ; loc++) {
        ident_line (out, ident);
        fprintf (out, "  \"%s\"\n", allegrex_disassemble (loc->opc, loc->address, FALSE));
        if (loc == op->end) break;
      }
      ident_line (out, ident);
      fprintf (out, "  : ");
      vel = list_head (op->results);
      while (vel) {
        struct value *val = element_getvalue (vel);
        if (vel != list_head (op->results))
          fprintf (out, ", ");
        fprintf (out, "\"=r\"(");
        print_value (out, val);
        fprintf (out, ")");
        vel = element_next (vel);
      }
      fprintf (out, "\n");
      ident_line (out, ident);
      fprintf (out, "  : ");
      vel = list_head (op->results);
      while (vel) {
        struct value *val = element_getvalue (vel);
        if (vel != list_head (op->results))
          fprintf (out, ", ");
        fprintf (out, "\"r\"(");
        print_value (out, val);
        fprintf (out, ")");
        vel = element_next (vel);
      }
      fprintf (out, "\n");
      ident_line (out, ident);
      fprintf (out, ");\n");
    } else if (op->type == OP_INSTRUCTION) {
      fprintf (out, "ops\n");
    } else {
      report ("Merda %d\n", op->type);
    }

    el = element_next (el);
  }
}

static
void print_subroutine (FILE *out, struct code *c, struct subroutine *sub)
{
  element el;
  int ident;

  if (sub->import) return;

  fprintf (out, "/**\n * Subroutine at address 0x%08X\n", sub->begin->address);
  fprintf (out, " */\n");
  fprintf (out, "void ");
  print_subroutine_name (out, sub);
  fprintf (out, " (void)\n{\n");

  if (sub->haserror) {
    struct location *loc;
    for (loc = sub->begin; ; loc++) {
      fprintf (out, "%s\n", allegrex_disassemble (loc->opc, loc->address, TRUE));
      if (loc == sub->end) break;
    }
  } else {
    el = list_head (sub->dfsblocks);
    while (el) {
      struct basicblock *block = element_getvalue (el);
      ident = 1;
      print_block (out, ident, block);
      fprintf (out, "\n");
      el = element_next (el);
    }
  }
  fprintf (out, "}\n\n");
}

static
void print_source (FILE *out, struct code *c, char *headerfilename)
{
  uint32 i, j;
  element el;

  fprintf (out, "#include <pspsdk.h>\n");
  fprintf (out, "#include \"%s\"\n\n", headerfilename);

  for (i = 0; i < c->file->modinfo->numimports; i++) {
    struct prx_import *imp = &c->file->modinfo->imports[i];

    fprintf (out, "/*\n * Imports from library: %s\n */\n", imp->name);
    for (j = 0; j < imp->nfuncs; j++) {
      struct prx_function *func = &imp->funcs[j];
      fprintf (out, "extern ");
      if (func->name) {
        fprintf (out, "void %s (void);\n",func->name);
      } else {
        fprintf (out, "void %s_%08X (void);\n", imp->name, func->nid);
      }
    }
    fprintf (out, "\n");
  }

  el = list_head (c->subroutines);
  while (el) {
    struct subroutine *sub;
    sub = element_getvalue (el);

    print_subroutine (out, c, sub);
    el = element_next (el);
  }

}

int print_code (struct code *c, char *prxname)
{
  char buffer[64];
  char basename[32];
  FILE *cout, *hout;


  get_base_name (prxname, basename, sizeof (basename));
  sprintf (buffer, "%s.c", basename);

  cout = fopen (buffer, "w");
  if (!cout) {
    xerror (__FILE__ ": can't open file for writing `%s'", buffer);
    return 0;
  }

  sprintf (buffer, "%s.h", basename);
  hout = fopen (buffer, "w");
  if (!hout) {
    xerror (__FILE__ ": can't open file for writing `%s'", buffer);
    return 0;
  }


  print_header (hout, c, buffer);
  print_source (cout, c, buffer);

  fclose (cout);
  fclose (hout);
  return 1;
}



static
void print_subroutine_graph (FILE *out, struct code *c, struct subroutine *sub)
{
  struct basicblock *block;
  element el, ref;

  fprintf (out, "digraph sub_%05X {\n", sub->begin->address);
  el = list_head (sub->blocks);

  while (el) {
    block = element_getvalue (el);

    fprintf (out, "    %3d ", block->node.dfsnum);
    fprintf (out, "[label=\"(%d) ", block->node.dfsnum);
    switch (block->type) {
    case BLOCK_START: fprintf (out, "Start");   break;
    case BLOCK_END: fprintf (out, "End");       break;
    case BLOCK_CALL: fprintf (out, "Call");     break;
    case BLOCK_SWITCH: fprintf (out, "Switch"); break;
    case BLOCK_SIMPLE: fprintf (out, "0x%08X", block->val.simple.begin->address);
    }
    fprintf (out, "\"];\n");


    if (block->revnode.dominator && list_size (block->outrefs) > 1) {
      fprintf (out, "    %3d -> %3d [color=green];\n", block->node.dfsnum, block->revnode.dominator->node.dfsnum);
    }

    /*
    if (list_size (block->frontier) != 0) {
      fprintf (out, "    %3d -> { ", block->dfsnum);
      ref = list_head (block->frontier);
      while (ref) {
        struct basicblock *refblock = element_getvalue (ref);
        fprintf (out, "%3d ", refblock->dfsnum);
        ref = element_next (ref);
      }
      fprintf (out, " } [color=green];\n");
    }
    */

    if (list_size (block->outrefs) != 0) {
      ref = list_head (block->outrefs);
      while (ref) {
        struct basicblock *refblock;
        refblock = element_getvalue (ref);
        fprintf (out, "    %3d -> %3d ", block->node.dfsnum, refblock->node.dfsnum);
        if (ref != list_head (block->outrefs))
          fprintf (out, "[arrowtail=dot]");

        if (refblock->node.parent == block) {
          fprintf (out, "[style=bold]");
        } else if (block->node.dfsnum >= refblock->node.dfsnum) {
          fprintf (out, "[color=red]");
        }
        fprintf (out, " ;\n");
        ref = element_next (ref);
      }
    }
    el = element_next (el);
  }
  fprintf (out, "}\n");
}


int print_graph (struct code *c, char *prxname)
{
  char buffer[64];
  char basename[32];
  element el;
  FILE *fp;
  int ret = 1;

  get_base_name (prxname, basename, sizeof (basename));

  el = list_head (c->subroutines);
  while (el) {
    struct subroutine *sub = element_getvalue (el);
    if (!sub->haserror && !sub->import) {
      sprintf (buffer, "%s_%08X.dot", basename, sub->begin->address);
      fp = fopen (buffer, "w");
      if (!fp) {
        xerror (__FILE__ ": can't open file for writing `%s'", buffer);
        ret = 0;
      } else {
        print_subroutine_graph (fp, c, sub);
        fclose (fp);
      }
    } else {
      if (sub->haserror) report ("Skipping subroutine at 0x%08X\n", sub->begin->address);
    }
    el = element_next (el);
  }

  return ret;
}
