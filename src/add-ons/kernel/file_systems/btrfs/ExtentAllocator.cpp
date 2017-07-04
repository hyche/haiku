#include "ExtentAllocator.h"
#include "Chunk.h"


struct FreeExtent : AVLTreeNode {
	uint64		offset;
	uint64		length;
	uint64		flags;

	FreeExtent(uint64 offset, uint64 length, uint64 flags)
	{
		this->offset = offset;
		this->length = length;
		this->flags = flags;
	}

	uint64 End() const
	{
		return offset + length;
	}

	bool Intersect(const FreeExtent* other)
	{
		if (offset >= other->End() || End() <= other->offset) {
			return false;
		}
		if (offset >= other->offset) {
			length = std::min(length, other->End() - offset);
		} else {
			offset = other->offset;
			length = std::min(other->length, End() - other->offset);
		}
		return true;
	}

	void info() const
	{
		TRACE("free extent at %" B_PRIu64 "( %" B_PRIu64 " ) length %" B_PRIu64
		" flags %" B_PRIu64 "\n", offset, offset/16384, length/16384, flags);
	}
};


struct TreeDefinition {
	typedef uint64			Key;
	typedef FreeExtent		Value;

	AVLTreeNode* GetAVLTreeNode(Value* value) const
	{
		return value;
	}

	Value* GetValue(AVLTreeNode* node) const
	{
		return static_cast<Value*>(node);
	}

	int Compare(const Key& a, const Value* b) const
	{
		if (a < b->offset)
			return -1;
		if (a >= b->End())
			return 1;
		return 0;
	}

	int Compare(const Value* a, const Value* b) const
	{
		int comp = Compare(a->offset, b);
		//TODO: check more conditions here for inserting
		return comp;
	}
};


struct FreeExtentTree : public AVLTree<TreeDefinition> {
	FreeExtentTree(const TreeDefinition& definition)
		:
		AVLTree<TreeDefinition>(definition)
	{
	}

	void DumpInOrder() const
	{
		_DumpInOrder(RootNode());
	}

private:
	void _DumpInOrder(FreeExtent* node) const
	{
		if (node == NULL)
			return;
		_DumpInOrder(fDefinition.GetValue(node->left));
		node->info();
		_DumpInOrder(fDefinition.GetValue(node->right));
	}
};


//BlockGroup


BlockGroup::BlockGroup(BTree* extentTree)
	:
	fBlockGroup(NULL),
	fExtentTree(extentTree)
{
	if (Allocate(BTRFS_BLOCKGROUP_FLAG_METADA) != B_OK) {
		panic("Fail to initliaze BlockGroup\n");
	}
}


BlockGroup::BlockGroup(BTree* extentTree, uint64 flag)
	:
	fBlockGroup(NULL),
	fExtentTree(extentTree)
{
	if (Allocate(flag) != B_OK) {
		panic("Fail to initliaze BlockGroup\n");
	}
}


BlockGroup::~BlockGroup()
{
	if (fBlockGroup != NULL)
		free(fBlockGroup);
}


// Allocate BlockGroup that has flag
status_t
BlockGroup::Allocate(uint64 flag)
{
	Chunk* chunk = fExtentTree->SystemVolume()->SystemChunk();
	fKey.SetObjectID(chunk->Offset());
	fKey.SetType(BTRFS_KEY_TYPE_BLOCKGROUP_ITEM);
	fKey.SetOffset(0);
	status_t status;

	while(true) {
		status = fExtentTree->FindNext(fKey, (void**)&fBlockGroup);
		if ((fBlockGroup->Flags() & flag) == flag || status != B_OK)
			break;
		fKey.SetObjectID(End());
		fKey.SetOffset(0);
	}

	if (status != B_OK) {
		TRACE("BlockGroup::Allocate() cannot find block group has flag: %"
			B_PRIu64 "\n", flag);
	}

	return status;
}


status_t
BlockGroup::LoadExtent(FreeExtentTree* tree, bool inverse)
{
	btrfs_key searchKey;
	void* data;
	status_t status;
	uint64 flags = fBlockGroup->flags;
	uint64 start = Start();

	searchKey.SetType(BTRFS_KEY_TYPE_METADATA_ITEM);
	uint64 extentSize = fExtentTree->SystemVolume()->BlockSize();
	if ((flags & BTRFS_BLOCKGROUP_FLAG_MASK) == BTRFS_BLOCKGROUP_FLAG_DATA)
		searchKey.SetType(BTRFS_KEY_TYPE_EXTENT_ITEM);
	searchKey.SetObjectID(start);
	searchKey.SetOffset(0);

	while(true) {
		status = fExtentTree->FindNext(searchKey, &data);
		if (status != B_OK) {
			if (searchKey.ObjectID() == Start()) {
				//handle special case when key has objectid == BlockGroup's objectid
				searchKey.SetObjectID(Start() + 1);
				searchKey.SetOffset(0);
				continue;
			}
			break;
		}
		if ((flags & BTRFS_BLOCKGROUP_FLAG_MASK) == BTRFS_BLOCKGROUP_FLAG_DATA) {
			extentSize = searchKey.Offset();
		}

		if (inverse == false) {
		} else {
			_InsertFreeExtent(tree, start, searchKey.ObjectID() - start, flags);
		}
		start = searchKey.ObjectID() + extentSize;
		searchKey.SetObjectID(start);
		searchKey.SetOffset(0);
	}
	if (inverse == true) {
		_InsertFreeExtent(tree, start, End() - start, flags);
	}

	return B_OK;
}


status_t
BlockGroup::_InsertFreeExtent(FreeExtentTree* tree, uint64 start, uint64 length,
	uint64 flags)
{
	if (length <= 0)
		return B_BAD_DATA;
	FreeExtent* freeExtent = new FreeExtent(start, length, flags);
	tree->Insert(freeExtent);
	return B_OK;
}


// ExtentAllocator


ExtentAllocator::ExtentAllocator(Volume* volume)
	:
	fTree(NULL),
	fBlockGroup(NULL),
	fVolume(volume)
{
	fTree = new FreeExtentTree(TreeDefinition());
	fBlockGroup = new BlockGroup(volume->ExtentTree());
}


ExtentAllocator::~ExtentAllocator()
{
	delete fTree;
	delete fBlockGroup;
}


void
ExtentAllocator::AllocateFreeExtent()
{
	BTree* extentTree = new BTree(fVolume);
	fBlockGroup->LoadExtent(fTree, true);
	fTree->DumpInOrder();
	delete extentTree;
}
