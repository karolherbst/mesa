#include "codegen/nv50_ir_driver.h"
#include "codegen/nv50_ir.h"
#include "codegen/nv50_ir_target.h"
#include "nv50_ir_driver.h"

static void
print_reloc(struct nv50_ir_prog_info_out *info_out) {
   printf("      \"RelocInfo\":");
   if (!info_out->bin.relocData) {
      printf("\"NULL\",\n");
      return;
   }
   nv50_ir::RelocInfo *reloc = (nv50_ir::RelocInfo *)info_out->bin.relocData;
   printf("{\n");
   printf("         \"codePos\":\"%d\",\n", reloc->codePos);
   printf("         \"libPos\":\"%d\",\n", reloc->libPos);
   printf("         \"dataPos\":\"%d\",\n", reloc->dataPos);
   printf("         \"count\":\"%d\",\n", reloc->count);
   printf("         \"RelocEntry\":[\n");
   for (unsigned int i = 0; i < reloc->count; i++) {
      printf("            {\"data\":\"%d\",\t\"mask\":\"%d\",\t\"offset\":\"%d\",\t\"bitPos\":\"%d\",\t\"type\":\"%d\"}",
                reloc->entry[i].data, reloc->entry[i].mask, reloc->entry[i].offset, reloc->entry[i].bitPos, reloc->entry[i].type
                );
      if (i != reloc->count + 1)
         printf(",\n");
   }
   printf("\n");
   printf("         ]\n");
   printf("      },\n");
}

static void
print_fixup(struct nv50_ir_prog_info_out *info_out) {
   printf("      \"FixupInfo\":");
   if (!info_out->bin.fixupData) {
      printf("\"NULL\"\n");
      return;
   }
   nv50_ir::FixupInfo *fixup = (nv50_ir::FixupInfo *)info_out->bin.fixupData;
   printf("{\n");
   printf("         \"count\":\"%d\"\n", fixup->count);
   printf("         \"FixupEntry\":[\n");
   for (unsigned int i = 0; i < fixup->count; i++) {
      printf("            {\"ipa\":\"%d\",\t\"reg\":\"%d\",\t\"loc\":\"%d\"}",
                fixup->entry[i].ipa, fixup->entry[i].reg, fixup->entry[i].loc);
      if (i != fixup->count + 1)
         printf(",\n");
   }
   printf("\n");
   printf("         ]\n");
   printf("      }\n");
}

extern void
nv50_ir_info_out_print(struct nv50_ir_prog_info_out *info_out)
{
   int i;

   printf("{\n");
   printf("   \"target\":\"%d\",\n", info_out->target);
   printf("   \"type\":\"%d\",\n", info_out->type);

   printf("   \"bin\":{\n");
   printf("      \"maxGPR\":\"%d\",\n", info_out->bin.maxGPR);
   printf("      \"tlsSpace\":\"%d\",\n", info_out->bin.tlsSpace);
   printf("      \"smemSize\":\"%d\",\n", info_out->bin.smemSize);
   printf("      \"codeSize\":\"%d\",\n", info_out->bin.codeSize);
   printf("      \"instructions\":\"%d\",\n", info_out->bin.instructions);
   print_reloc(info_out);
   print_fixup(info_out);
   printf("   },\n");

   if (info_out->numSysVals) {
      printf("   \"sv\":[\n");
      for (i = 0; i < info_out->numSysVals; i++) {
         if (&(info_out->sv[i])) {
            printf("      {\"id\":\"%d\", \"sn\":\"%d\", \"si\":\"%d\"}",
                   info_out->sv[i].id, info_out->sv[i].sn, info_out->sv[i].si);
            if (i != info_out->numSysVals - 1)
               printf(",\n");
         }
      }
      printf("\n   ],\n");
   }
   if (info_out->numInputs) {
      printf("   \"in\":[\n");
      for (i = 0; i < info_out->numInputs; i++) {
         if (&(info_out->in[i])) {
            printf("      {\"id\":\"%d\",\t\"sn\":\"%d\",\t\"si\":\"%d\"}",
                info_out->in[i].id, info_out->in[i].sn, info_out->in[i].si);
            if (i != info_out->numInputs - 1)
               printf(",\n");
         }
      }
      printf("\n   ],\n");
   }
   if (info_out->numOutputs) {
      printf("   \"out\":[\n");
      for (i = 0; i < info_out->numOutputs; i++) {
         if (&(info_out->out[i])) {
            printf("      {\"id\":\"%d\",\t\"sn\":\"%d\",\t\"si\":\"%d\"}",
                   info_out->out[i].id, info_out->out[i].sn, info_out->out[i].si);
            if (i != info_out->numOutputs - 1)
                printf(",\n");
         }
      }
      printf("\n   ],\n");
   }

   printf("   \"numInputs\":\"%d\",\n", info_out->numInputs);
   printf("   \"numOutputs\":\"%d\",\n", info_out->numOutputs);
   printf("   \"numPatchConstants\":\"%d\",\n", info_out->numPatchConstants);
   printf("   \"numSysVals\":\"%d\",\n", info_out->numSysVals);

   printf("   \"prop\":{\n");
   switch (info_out->type) {
      case PIPE_SHADER_VERTEX:
         printf("      \"vp\": {\"usesDrawParameters\":\"%s\"}\n",
               info_out->prop.vp.usesDrawParameters ? "true" : "false");
         break;
      case PIPE_SHADER_TESS_CTRL:
      case PIPE_SHADER_TESS_EVAL:
         printf("      \"tp\":{\n");
         printf("         \"outputPatchSize\":\"%d\"\n", info_out->prop.tp.outputPatchSize);
         printf("         \"partitioning\":\"%d\"\n", info_out->prop.tp.partitioning);
         printf("         \"winding\":\"%d\"\n", info_out->prop.tp.winding);
         printf("         \"domain\":\"%d\"\n", info_out->prop.tp.domain);
         printf("         \"outputPrim\":\"%d\"\n", info_out->prop.tp.outputPrim);
         break;
      case PIPE_SHADER_GEOMETRY:
         printf("      \"gp\":{\n");
         printf("         \"outputPrim\":\"%d\"\n", info_out->prop.gp.outputPrim);
         printf("         \"instancesCount\":\"%d\"\n", info_out->prop.gp.instanceCount);
         printf("         \"maxVertices\":\"%d\"\n", info_out->prop.gp.maxVertices);
         break;
      case PIPE_SHADER_FRAGMENT:
         printf("      \"fp\":{\n");
         printf("         \"numColourResults\":\"%d\"\n", info_out->prop.fp.numColourResults);
         printf("         \"writesDepth\":\"%s\"\n", info_out->prop.fp.writesDepth ? "true" : "false");
         printf("         \"earlyFragTests\":\"%s\"\n", info_out->prop.fp.earlyFragTests ? "true" : "false");
         printf("         \"postDepthCoverage\":\"%s\"\n", info_out->prop.fp.postDepthCoverage ? "true" : "false");
         printf("         \"usesDiscard\":\"%s\"\n", info_out->prop.fp.usesDiscard ? "true" : "false");
         printf("         \"usesSampleMaskIn\":\"%s\"\n", info_out->prop.fp.usesSampleMaskIn ? "true" : "false");
         printf("         \"readsFramebuffer\":\"%s\"\n", info_out->prop.fp.readsFramebuffer ? "true" : "false");
         printf("         \"readsSampleLocations\":\"%s\"\n", info_out->prop.fp.readsSampleLocations ? "true" : "false");
         break;
      default:
         assert("!unhandled pipe shader type\n");
   }
   printf("      }\n");
   printf("   }\n");

   printf("   \"io\":{\n");
   printf("      \"clipDistances\":\"%d\"\n", info_out->io.clipDistances);
   printf("      \"cullDistances\":\"%d\"\n", info_out->io.cullDistances);
   printf("      \"genUserClip\":\"%d\"\n", info_out->io.genUserClip);
   printf("      \"instanceId\":\"%d\"\n", info_out->io.instanceId);
   printf("      \"vertexId\":\"%hhd\"\n", info_out->io.vertexId);
   printf("      \"edgeFlagIn\":\"%d\"\n", info_out->io.edgeFlagIn);
   printf("      \"edgeFlagOut\":\"%d\"\n", info_out->io.edgeFlagOut);
   printf("      \"fragDepth\":\"%d\"\n", info_out->io.fragDepth);
   printf("      \"sampleMask\":\"%d\"\n", info_out->io.sampleMask);
   printf("      \"globalAccess\":\"%d\"\n", info_out->io.globalAccess);
   printf("      \"fp64\":\"%s\"\n", info_out->io.fp64 ? "true" : "false");
   printf("   \"}\n");
   printf("   \"numBarriers\":\"%d\"\n", info_out->numBarriers);

   printf("}\n");
}

