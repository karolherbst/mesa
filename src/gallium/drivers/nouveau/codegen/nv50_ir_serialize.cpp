#include "util/blob.h"
#include "codegen/nv50_ir_driver.h"
#include "codegen/nv50_ir.h"
#include "codegen/nv50_ir_target.h"
#include "nv50_ir_driver.h"


static void
serialize_bin(struct blob *blob, struct nv50_ir_prog_info_out *info_out)
{
   blob_write_uint16(blob, info_out->bin.maxGPR);
   blob_write_uint32(blob, info_out->bin.tlsSpace);
   blob_write_uint32(blob, info_out->bin.smemSize);
   blob_write_uint32(blob, info_out->bin.codeSize);
   blob_write_bytes(blob, info_out->bin.code, info_out->bin.codeSize);
   blob_write_uint32(blob, info_out->bin.instructions);
   
   if (info_out->bin.relocData) {
         printf("Serializing RelocData\n\n");
      blob_write_uint8(blob, true);
      //blob_overwrite_uint8(blob, blob->size, true);
      nv50_ir::RelocInfo *reloc = (nv50_ir::RelocInfo *)info_out->bin.relocData;
      blob_write_uint32(blob, reloc->codePos);
      blob_write_uint32(blob, reloc->libPos);
      blob_write_uint32(blob, reloc->dataPos);
      blob_write_uint32(blob, reloc->count);
      blob_write_bytes(blob, reloc->entry, sizeof(*reloc->entry) * reloc->count);
   }
   else {
      blob_write_uint8(blob, false);
   }

   //blob_write_uint8(blob, false);
   if (info_out->bin.fixupData) {
      //blob_overwrite_uint8(blob, blob->size, true);
      printf("Serializing FixupData\n\n");
      blob_write_uint8(blob, true);
      /* TODO: Add (FixupEntry)entry[i].apply */
      nv50_ir::FixupInfo *fixup = (nv50_ir::FixupInfo *)info_out->bin.fixupData;
      blob_write_uint32(blob, fixup->count);
      for (unsigned int i = 0; i < fixup->count; i++) {
         blob_write_uint32(blob, fixup->entry[i].ipa);
         blob_write_uint32(blob, fixup->entry[i].reg);
         blob_write_uint32(blob, fixup->entry[i].loc);
         if (fixup->entry[i].apply == NULL)
            blob_write_uint8(blob, 0);
         else if (fixup->entry[i].apply == nv50_ir::nv50_interpApply)
            blob_write_uint8(blob, 1);
         else if (fixup->entry[i].apply == nv50_ir::nvc0_interpApply)
            blob_write_uint8(blob, 2);
         else if (fixup->entry[i].apply == nv50_ir::gk110_interpApply)
            blob_write_uint8(blob, 3);
         else if (fixup->entry[i].apply == nv50_ir::gm107_interpApply)
            blob_write_uint8(blob, 4);
         else {
            blob_write_uint8(blob, 0);
            assert(!"unhandled fixup apply function pointer\n");
         }
      }
   }
   else {
      blob_write_uint8(blob, false);
   }
}

extern void
nv50_ir_info_out_serialize(struct blob *blob,
                                  struct nv50_ir_prog_info_out *info_out)
{
   blob_write_uint16(blob, info_out->target);
   blob_write_uint8(blob, info_out->type);
   serialize_bin(blob, info_out);
   blob_write_bytes(blob, info_out->sv, sizeof(info_out->sv));
   blob_write_bytes(blob, info_out->in, sizeof(info_out->in));
   blob_write_bytes(blob, info_out->out, sizeof(info_out->out));
   blob_write_uint8(blob, info_out->numInputs);
   blob_write_uint8(blob, info_out->numOutputs);
   blob_write_uint8(blob, info_out->numPatchConstants);
   blob_write_uint8(blob, info_out->numSysVals);
   blob_write_bytes(blob, &(info_out->prop), sizeof(info_out->prop));
   blob_write_bytes(blob, &(info_out->io), sizeof(info_out->io));
   blob_write_uint8(blob, info_out->numBarriers);
}

static void
deserialize_bin(struct blob_reader *reader, nv50_ir_prog_info_out *info_out)
{
   info_out->bin.maxGPR = blob_read_uint16(reader);
   info_out->bin.tlsSpace = blob_read_uint32(reader);
   info_out->bin.smemSize = blob_read_uint32(reader);
   info_out->bin.codeSize = blob_read_uint32(reader);
   info_out->bin.code = (uint32_t *)malloc(info_out->bin.codeSize);
   blob_copy_bytes(reader, info_out->bin.code, info_out->bin.codeSize);
   info_out->bin.instructions = blob_read_uint32(reader);

   info_out->bin.relocData = NULL;
   if (blob_read_uint8(reader) == true) {
      printf("Deserializing RelocData\n\n\n");
      nv50_ir::RelocInfo *reloc = (nv50_ir::RelocInfo *)
                                                 malloc(sizeof(nv50_ir::RelocInfo));
      reloc->codePos = blob_read_uint32(reader);
      reloc->libPos = blob_read_uint32(reader);
      reloc->dataPos = blob_read_uint32(reader);
      reloc->count = blob_read_uint32(reader);
      blob_copy_bytes(reader, reloc->entry, sizeof(*reloc->entry) * reloc->count);

      info_out->bin.relocData = reloc;
   }

   info_out->bin.fixupData = NULL;
   if (blob_read_uint8(reader) == true) {
      printf("Deserializing FixupData\n\n\n");
      uint32_t count = blob_read_uint32(reader);
      nv50_ir::FixupInfo *fixup =
         CALLOC_VARIANT_LENGTH_STRUCT(nv50_ir::FixupInfo,
               count * sizeof(*fixup->entry));
      fixup->count = count;
      for (unsigned int i = 0; i < fixup->count; i++) {
         fixup->entry[i].ipa = blob_read_uint32(reader);
         fixup->entry[i].reg = blob_read_uint32(reader);
         fixup->entry[i].loc = blob_read_uint32(reader);
         uint8_t test = blob_read_uint8(reader);
         switch(test) {
            case 0:
               fixup->entry[i].apply = NULL;
               break;
            case 1:
               fixup->entry[i].apply = nv50_ir::nv50_interpApply;  
               break;
            case 2:
               fixup->entry[i].apply = nv50_ir::nvc0_interpApply;
               break;
            case 3:
               fixup->entry[i].apply = nv50_ir::gk110_interpApply;
               break;
            case 4:
               fixup->entry[i].apply = nv50_ir::gm107_interpApply;
               break;
            default:
               assert(!"unhandled apply function switch case\n");
         }
      }
      info_out->bin.fixupData = fixup;
   }
}

extern void
nv50_ir_info_out_deserialize(struct blob *blob, struct nv50_ir_prog_info_out
      *info_out)
{
   struct blob_reader reader;
   blob_reader_init(&reader, blob->data, blob->size);

   info_out->target = blob_read_uint16(&reader);
   info_out->type = blob_read_uint8(&reader);
   deserialize_bin(&reader, info_out);
   blob_copy_bytes(&reader, info_out->sv, sizeof(info_out->sv));
   blob_copy_bytes(&reader, info_out->in, sizeof(info_out->in));
   blob_copy_bytes(&reader, info_out->out, sizeof(info_out->out));
   info_out->numInputs = blob_read_uint8(&reader);
   info_out->numOutputs = blob_read_uint8(&reader);
   info_out->numPatchConstants = blob_read_uint8(&reader);
   info_out->numSysVals = blob_read_uint8(&reader);
   blob_copy_bytes(&reader, &(info_out->prop), sizeof(info_out->prop));
   blob_copy_bytes(&reader, &(info_out->io), sizeof(info_out->io));
   info_out->numBarriers = blob_read_uint8(&reader);
}
