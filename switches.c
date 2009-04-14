
#include <string.h>

#include "code.h"
#include "utils.h"

static
int analyse_switch (struct code *c, struct codeswitch *cs)
{
  struct location *loc = cs->location;
  element el;
  uint32 gpr;

  if (!loc->insn) return 0;

  if (loc->insn->insn == I_LW) {
    gpr = loc->gpr_defined;
    while (1) {
      if (loc++ == c->end) return 0;
      if (!loc->insn) return 0;
      if (loc->gpr_used & gpr) {
        if (loc->insn->insn != I_JR)
          return 0;
        break;
      }
      if (loc->insn->flags & (INSN_JUMP | INSN_BRANCH)) return 0;
    }
  } else if (loc->insn->insn == I_ADDIU) {
    int count = 0;
    gpr = loc->gpr_defined;
    while (1) {
      if (loc++ == c->end) return 0;
      if (!loc->insn) return 0;
      if (loc->gpr_used & gpr) {
        if (count == 0) {
          if (loc->insn->insn != I_ADDU) return 0;
        } else if (count == 1) {
          if (loc->insn->insn != I_LW) return 0;
        } else {
          if (loc->insn->insn != I_JR)
            return 0;
          break;
        }
        count++;
        gpr = loc->gpr_defined;
      }
      if (loc->insn->flags & (INSN_JUMP | INSN_BRANCH)) return 0;
    }
  } else return 0;

  cs->jumplocation = loc;
  cs->jumplocation->cswitch = cs;

  el = list_head (cs->references);
  while (el) {
    struct location *target = element_getvalue (el);
    target->switchtarget = TRUE;
    if (!target->references)
      target->references = list_alloc (c->lstpool);
    list_inserttail (target->references, cs->jumplocation);

    el = element_next (el);
  }
  return 1;
}

void extract_switches (struct code *c)
{
  struct prx_reloc *aux;
  uint32 base, end, count = 0;
  uint32 i, j, tgt;

  c->switches = list_alloc (c->lstpool);

  for (i = 0; i < c->file->relocnum; i++) {
    struct prx_reloc *rel = &c->file->relocsbyaddr[i];
    count = 0;
    do {
      if (rel->type != R_MIPS_32) break;
      tgt = (rel[count].target - c->baddr) >> 2;
      if (tgt >= c->numopc) break;
      if (rel[count].target & 0x03) break;

      count++;
    } while ((i + count) < c->file->relocnum);

    if (count == 0) continue;

    base = end = prx_findreloc (c->file, rel->vaddr);
    if (base >= c->file->relocnum) continue;
    if (c->file->relocs[base].target != rel->vaddr) continue;

    for (; end < c->file->relocnum; end++) {
      aux = &c->file->relocs[end];
      if (aux->target != rel->vaddr) {
        if (aux->target & 0x03) {
          error (__FILE__ ": relocation target not word aligned 0x%08X", aux->target);
          count = 0;
        } else if (count > ((aux->target - rel->vaddr) >> 2))
          count = (aux->target - rel->vaddr) >> 2;
        break;
      }
    }

    if (count <= 1) continue;

    for (;base < end; base++) {
      aux = &c->file->relocs[base];
      tgt = (aux->vaddr - c->baddr) >> 2;
      if (tgt >= c->numopc) continue;
      if (aux->vaddr & 0x03) {
        error (__FILE__ ": relocation vaddr not word aligned 0x%08X", aux->vaddr);
        continue;
      }

      if (aux->type == R_MIPS_LO16) {
        struct codeswitch *cs;

        cs = fixedpool_alloc (c->switchpool);
        memset (cs, 0, sizeof (struct codeswitch));

        cs->jumpreloc = aux;
        cs->switchreloc = rel;
        cs->location = &c->base[tgt];
        cs->count = count;
        cs->references = list_alloc (c->lstpool);
        for (j = 0; j < count; j++) {
          tgt = (rel[j].target - c->baddr) >> 2;
          list_inserttail (cs->references, &c->base[tgt]);
        }

        if (analyse_switch (c, cs)) {
          list_inserttail (c->switches, cs);
        } else {
          fixedpool_free (c->switchpool, cs);
        }
      }
    }
  }
}


