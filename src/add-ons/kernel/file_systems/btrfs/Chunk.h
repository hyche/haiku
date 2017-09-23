/*
 * Copyright 2011, Haiku Inc. All rights reserved.
 * This file may be used under the terms of the MIT License.
 *
 * Authors:
 *		Jérôme Duval
 *		Chế Vũ Gia Hy
 */
#ifndef CHUNK_H
#define CHUNK_H


#include "btrfs.h"
#include "Volume.h"


class Chunk {
public:
								Chunk(btrfs_chunk* chunk,
									fsblock_t offset);
								~Chunk();
			uint32				Size() const;
			status_t			FindBlock(off_t logical, off_t& physical);
			fsblock_t			Offset() const { return fChunkOffset; }
			fsblock_t			End() const
									{ return fChunkOffset + fChunk->Length(); }
private:
			btrfs_chunk*	fChunk;
			fsblock_t			fChunkOffset;
			status_t			fInitStatus;
};


class DevExtent {
public:
								DevExtent(Volume* volume);
								DevExtent(Volume* volume, off_t physical);
								~DevExtent();
			uint64				Offset() const { return fKey.Offset(); }
			uint64				End() const;
			void				Init(off_t physical);
			off_t				ToLogical(off_t physical);


private:
			Volume*				fVolume;
			btrfs_dev_extent*	fDevExtent;
			btrfs_key			fKey;
};


#endif	// CHUNK_H
