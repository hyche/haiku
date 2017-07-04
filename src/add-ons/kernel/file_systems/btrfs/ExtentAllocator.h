#ifndef EXTENT_ALLOCATOR_H
#define EXTENT_ALLOCATOR_H


#include "Volume.h"
#include "BTree.h"


//#define TRACE_BTRFS
#ifdef TRACE_BTRFS
#	define TRACE(x...) dprintf("\33[34mbtrfs:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif


struct FreeExtent;
struct FreeExtentTree;


class BlockGroup {
public:
								BlockGroup(BTree* extentTree);
								BlockGroup(BTree* extentTree, uint64 flag);
								~BlockGroup();

			status_t			Allocate(uint64 flag);
			status_t			LoadExtent(FreeExtentTree* tree,
									bool inverse=false);
			uint64				Start() const { return fKey.ObjectID(); }
			uint64				End() const
				{ return fKey.ObjectID() + fKey.Offset(); }

private:
								BlockGroup(const BlockGroup&);
								BlockGroup& operator=(const BlockGroup&);
			status_t			_InsertFreeExtent(FreeExtentTree* tree,
									uint64 size, uint64 length, uint64 flags);

			btrfs_key			fKey;
			btrfs_block_group* 	fBlockGroup;
			BTree*				fExtentTree;
};


class ExtentAllocator {
public:
								ExtentAllocator(Volume* volume);
								~ExtentAllocator();

			void				AllocateFreeExtent();
private:
								ExtentAllocator(const ExtentAllocator&);
								ExtentAllocator& operator=(const ExtentAllocator&);
private:
			Volume*				fVolume;
			BlockGroup*			fBlockGroup;
			FreeExtentTree*		fTree;
};


#endif	// EXTENT_ALLOCATOR_H
