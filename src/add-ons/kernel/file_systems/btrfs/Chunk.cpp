/*
 * Copyright 2011, Haiku Inc. All rights reserved.
 * This file may be used under the terms of the MIT License.
 *
 * Authors:
 *		Jérôme Duval
 *		Chế Vũ Gia Hy
 */


#include "Chunk.h"
#include "BTree.h"


//#define TRACE_BTRFS
#ifdef TRACE_BTRFS
#	define TRACE(x...) dprintf("\33[34mbtrfs:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif
#	define FATAL(x...) dprintf("\33[34mbtrfs:\33[0m " x)


Chunk::Chunk(btrfs_chunk* chunk, fsblock_t offset)
	:
	fChunk(NULL),
	fInitStatus(B_OK)
{
	fChunkOffset = offset;
	fChunk = (btrfs_chunk*)malloc(sizeof(btrfs_chunk)
		+ chunk->StripeCount() * sizeof(btrfs_stripe));
	if (fChunk == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}

	memcpy(fChunk, chunk, sizeof(btrfs_chunk)
		+ chunk->StripeCount() * sizeof(btrfs_stripe));

	TRACE("chunk[0] length %" B_PRIu64 " owner %" B_PRIu64 " stripe_length %"
		B_PRIu64 " type %" B_PRIu64 " stripe_count %u sub_stripes %u "
		"sector_size %" B_PRIu32 "\n", chunk->Length(), chunk->Owner(),
		chunk->StripeLength(), chunk->Type(), chunk->StripeCount(),
		chunk->SubStripes(), chunk->SectorSize());
	for (int32 i = 0; i < chunk->StripeCount(); i++) {
		TRACE("chunk.stripe[%" B_PRId32 "].physical %" B_PRId64 " deviceid %"
			B_PRId64 "\n", i, chunk->stripes[i].Offset(),
			chunk->stripes[i].DeviceID());
	}
}


Chunk::~Chunk()
{
	free(fChunk);
}


uint32
Chunk::Size() const
{
	return sizeof(btrfs_chunk)
		+ fChunk->StripeCount() * sizeof(btrfs_stripe);
}


status_t
Chunk::FindBlock(off_t logical, off_t& physical)
{
	if (fChunk == NULL)
		return B_NO_INIT;

	if (logical < (off_t)fChunkOffset
		|| logical > (off_t)(fChunkOffset + fChunk->Length()))
		return B_BAD_VALUE;

	// only one stripe
	TRACE("Chunk::FindBlock() logical: %" B_PRIdOFF " stripe offset:%"
		B_PRIu64 " chunk offset: %" B_PRIu64 "\n", logical, fChunk->stripes[0].Offset(), fChunkOffset);
	physical = logical + fChunk->stripes[0].Offset() - fChunkOffset;
	return B_OK;
}


// DevExtent


DevExtent::DevExtent(Volume* volume)
	:
	fVolume(volume)
{
}


DevExtent::DevExtent(Volume* volume, off_t physical)
	:
	fVolume(volume)
{
}


DevExtent::~DevExtent()
{
	delete fDevExtent;
}


uint64
DevExtent::End() const
{
	if (Offset() == 0)
		return 0;
	return Offset() + fDevExtent->Length();
}


// Allocate DevExtent contains physical address "physical".
void
DevExtent::Init(off_t physical)
{
	fKey.SetObjectID(fVolume->ID());
	fKey.SetType(BTRFS_KEY_TYPE_DEVICE_EXTENT);
	fKey.SetOffset(physical);
	//if (fVolume->DevTree()->FindPrevious(fKey, (void**)&fDevExtent) != B_OK){
	//	TRACE("DevExtent::Init() not found dev extent offset: %" B_PRIu64 "\n",
	//		physical);
	//}
}


off_t
DevExtent::ToLogical(off_t physical)
{
	if (Offset() == 0)	// logical address is the same as physical address
		return physical;

	if (physical >= End() || physical < Offset())
		Init(physical);

	return physical - Offset() + fDevExtent->ChunkOffset();
}
