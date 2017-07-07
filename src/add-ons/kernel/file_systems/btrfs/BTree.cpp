/*
 * Copyright 2011, Jérôme Duval, korli@users.berlios.de.
 * Copyright 2001-2010, Axel Dörfler, axeld@pinc-software.de.
 * This file may be used under the terms of the MIT License.
 */


//! BTree implementation


#include "BTree.h"
#include "CachedBlock.h"


//#define TRACE_BTRFS
#ifdef TRACE_BTRFS
#	define TRACE(x...) dprintf("\33[34mbtrfs:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif
#	define ERROR(x...) dprintf("\33[34mbtrfs:\33[0m " x)


BTreeNode::BTreeNode(Volume* volume)
	:
	fNode(NULL),
	fVolume(volume),
	fBlockNumber(0),
	fCurrentSlot(0),
	fWritable(false)
{
}


BTreeNode::BTreeNode(Volume* volume, off_t block)
	:
	fNode(NULL),
	fVolume(volume),
	fBlockNumber(0),
	fCurrentSlot(0),
	fWritable(false)
{
	SetTo(block);
}


BTreeNode::~BTreeNode()
{
	Unset();
}


void
BTreeNode::Keep()
{
	fNode = NULL;
}

void
BTreeNode::Unset()
{
	if (fNode != NULL) {
		block_cache_put(fVolume->BlockCache(), fBlockNumber);
		fNode = NULL;
	}
}


void
BTreeNode::SetTo(off_t block)
{
	Unset();
	fBlockNumber = block;
	fNode = (btrfs_stream*)block_cache_get(fVolume->BlockCache(), block);
}


void
BTreeNode::SetToWritable(off_t block, int32 transactionId, bool empty)
{
	Unset();
	fBlockNumber = block;
	fWritable = true;
	if (empty) {
		fNode = (btrfs_stream*)block_cache_get_empty(fVolume->BlockCache(), block,
			transactionId);
	} else {
		fNode = (btrfs_stream*)block_cache_get_writable(fVolume->BlockCache(), block,
			transactionId);
	}
}


/*
 * calculate used space, 0 is for internal node, from 1 -> 3 is for leaf
 * type 0: calculate for internal nodes
 * type 1: only item space
 * type 2: only item data space
 * type 3: both type 1 and 2
 */
uint32
BTreeNode::_CalculateSpace(uint32 from, uint32 to, uint8 type) const
{
	if (to < from || from < 0 || to >= ItemCount() || ItemCount() == 0)
		return 0;

	uint32 result = 0;
	if (type == 0) {
		result = sizeof(btrfs_index) * (to - from + 1);
	}
	if ((type & 1) == 1) {
		result += sizeof(btrfs_entry) * (to - from + 1);
	}
	if ((type & 2) == 2) {
		result += Item(from)->Offset() + Item(from)->Size()
			- Item(to)->Offset();
	}
	return result + sizeof(btrfs_header);
}


uint32
BTreeNode::SpaceUsed() const
{
	if (Level() == 0)
		return _CalculateSpace(0, ItemCount() - 1, 3);
	return _CalculateSpace(0, ItemCount() - 1, 0);
}


uint32
BTreeNode::SpaceLeft() const
{
	return fVolume->BlockSize() - SpaceUsed();
}


void
BTreeNode::_Copy(const BTreeNode* origin, uint32 at, uint32 from,
	uint32 to) const
{
	if (Level() == 0) {
		memcpy(Item(at), origin->Item(from),
			origin->_CalculateSpace(from, to, 1));
		memcpy(ItemData(at), origin->ItemData(from),
			origin->_CalculateSpace(from, to, 2));
	} else {
		memcpy(Index(at), origin->Index(from),
			origin->_CalculateSpace(from, to, 0));
	}
}


/*
 * copy item and item data except its header
 * length < 0: removing
 * length > 0: inserting
 */
status_t
BTreeNode::Copy(const BTreeNode* origin, uint32 start, uint32 end,
	int length) const
{
	if (length < 0 && std::abs(length) >= SpaceUsed() - sizeof(btrfs_header)
		|| (length > 0 && length >= SpaceLeft()))
		return B_BAD_VALUE;

	if (length == 0) {
		//copy all
		_Copy(origin, 0, 0, ItemCount() - 1);
	} else if (length < 0) {
		//removing all items in [start, end]
		_Copy(origin, 0, 0, start - 1);	// <-- [start,...
		_Copy(origin, start, end + 1, ItemCount() - 1);	// ..., end] -->
	} else {
		//inserting in [start, end] - make a hole for later
		_Copy(origin, 0, 0, start - 1);
		_Copy(origin, end + 1, start, ItemCount() - 1);
	}

	return B_OK;
}


int32
BTreeNode::SearchSlot(const btrfs_key& key, int* slot, btree_traversing type) const
{
	//binary search for item slot in a node
	int entrySize = sizeof(btrfs_entry);
	if (Level() != 0) {
		// internal node
		entrySize = sizeof(btrfs_index);
	}

	int low = 0;
	int high = ItemCount();
	int mid, comp;
	int base = sizeof(btrfs_header);
	const btrfs_key* other;
	while (low < high) {
		mid = (low + high) / 2;
		other = (const btrfs_key*)((uint8*)fNode + base + mid * entrySize);
		comp = key.Compare(*other);
		if (comp < 0)
			high = mid;
		else if (comp > 0)
			low = mid + 1;
		else {
			*slot = mid;
			return B_OK; 		//if key is in node
		}
	}

	TRACE("SearchSlot() found slot %" B_PRId32 " comp %" B_PRId32 "\n",
		*slot, comp);
	if (type == BTREE_BACKWARD)
		*slot = low - 1;
	else if (type == BTREE_FORWARD)
		*slot = low;

	if (*slot < 0 || type == BTREE_EXACT)
		return B_ENTRY_NOT_FOUND;
	return B_OK;
}


//-pragma mark


BTree::BTree(Volume* volume)
	:
	fRootBlock(0),
	fVolume(volume)
{
	mutex_init(&fIteratorLock, "btrfs b+tree iterator");
}


BTree::BTree(Volume* volume, btrfs_stream* stream)
	:
	fRootBlock(0),
	fVolume(volume)
{
	mutex_init(&fIteratorLock, "btrfs b+tree iterator");
}


BTree::BTree(Volume* volume, fsblock_t rootBlock)
	:
	fRootBlock(rootBlock),
	fVolume(volume)
{
	mutex_init(&fIteratorLock, "btrfs b+tree iterator");
}


BTree::~BTree()
{
	// if there are any TreeIterators left, we need to stop them
	// (can happen when the tree's inode gets deleted while
	// traversing the tree - a TreeIterator doesn't lock the inode)
	mutex_lock(&fIteratorLock);

	SinglyLinkedList<TreeIterator>::Iterator iterator
		= fIterators.GetIterator();
	while (iterator.HasNext())
		iterator.Next()->Stop();
	mutex_destroy(&fIteratorLock);
}


int32
btrfs_key::Compare(const btrfs_key& key) const
{
	if (ObjectID() > key.ObjectID())
		return 1;
	if (ObjectID() < key.ObjectID())
		return -1;
	if (Type() > key.Type())
		return 1;
	if (Type() < key.Type())
		return -1;
	if (Offset() > key.Offset())
		return 1;
	if (Offset() < key.Offset())
		return -1;
	return 0;
}


/*!	Searches the key in the tree, and stores the allocated found item in
	_value, if successful.
	Returns B_OK when the key could be found, B_ENTRY_NOT_FOUND if not.
	It can also return other errors to indicate that something went wrong.
*/
status_t
BTree::_Find(btrfs_key& key, void** _value, size_t* _size,
	bool read, btree_traversing type)
{
	TRACE("Find() objectid %" B_PRId64 " type %d offset %" B_PRId64 " \n",
		key.ObjectID(),	key.Type(), key.Offset());
	CachedBlock cached(fVolume);
	BTreeNode node(fVolume, fRootBlock);
	int slot, ret;
	fsblock_t physicalBlock;

	while (node.Level() != 0) {
		TRACE("Find() level %d\n", node.Level());
		ret = node.SearchSlot(key, &slot, BTREE_BACKWARD);
		if (ret != B_OK)
			return ret;
		TRACE("Find() getting index %" B_PRIu32 "\n", slot);

		if (fVolume->FindBlock(node.Index(slot)->Logical(), physicalBlock)
			!= B_OK) {
			ERROR("Find() unmapped block %" B_PRId64 "\n",
				node.Index(slot)->Logical());
			return B_ERROR;
		}
		node.SetTo(physicalBlock);
	}

	TRACE("Find() dump count %" B_PRId32 "\n", node.ItemCount());
	ret = node.SearchSlot(key, &slot, type);
	if ((slot >= node.ItemCount() || node.Item(slot)->key.Type() != key.Type())
			&& read == true
		|| ret != B_OK) {
		TRACE("Find() not found %" B_PRId64 " %" B_PRId64 "\n", key.Offset(),
			key.ObjectID());
		return B_ENTRY_NOT_FOUND;
	}

	if (read == true) {
		TRACE("Find() found %" B_PRIu32 " %" B_PRIu32 "\n",
			node.Item(slot)->Offset(), node.Item(slot)->Size());

		if (_value != NULL) {
			*_value = malloc(node.Item(slot)->Size());
			memcpy(*_value, node.ItemData(slot),
				node.Item(slot)->Size());
			key.SetOffset(node.Item(slot)->key.Offset());
			key.SetObjectID(node.Item(slot)->key.ObjectID());
			if (_size != NULL)
				*_size = node.Item(slot)->Size();
		}
	} else {
		*_value = (void*)&slot;
	}
	return B_OK;
}


status_t
BTree::FindNext(btrfs_key& key, void** _value, size_t* _size, bool read)
{
	return _Find(key, _value, _size, read, BTREE_FORWARD);
}


status_t
BTree::FindPrevious(btrfs_key& key, void** _value, size_t* _size, bool read)
{
	return _Find(key, _value, _size, read, BTREE_BACKWARD);
}


status_t
BTree::FindExact(btrfs_key& key, void** _value, size_t* _size, bool read)
{
	return _Find(key, _value, _size, read, BTREE_EXACT);
}


status_t
BTree::SetRoot(off_t logical, fsblock_t* block)
{
	if (block != NULL) {
		fRootBlock = *block;
		//TODO: mapping physical block -> logical address
	} else {
		fLogicalRoot = logical;
		if (fVolume->FindBlock(logical, fRootBlock) != B_OK) {
			ERROR("Find() unmapped block %" B_PRId64 "\n", fRootBlock);
			return B_ERROR;
		}
	}
	return B_OK;
}


void
BTree::_AddIterator(TreeIterator* iterator)
{
	MutexLocker _(fIteratorLock);
	fIterators.Add(iterator);
}


void
BTree::_RemoveIterator(TreeIterator* iterator)
{
	MutexLocker _(fIteratorLock);
	fIterators.Remove(iterator);
}


//	#pragma mark -


TreeIterator::TreeIterator(BTree* tree, btrfs_key& key)
	:
	fTree(tree),
	fCurrentKey(key)
{
	Rewind();
	tree->_AddIterator(this);
}


TreeIterator::~TreeIterator()
{
	if (fTree)
		fTree->_RemoveIterator(this);
}


/*!	Iterates through the tree in the specified direction.
*/
status_t
TreeIterator::Traverse(btree_traversing direction, btrfs_key& key,
	void** value, size_t* size)
{
	if (fTree == NULL)
		return B_INTERRUPTED;

	fCurrentKey.SetOffset(fCurrentKey.Offset() + direction);
	status_t status = fTree->_Find(fCurrentKey, value, size,
		true, direction);
	if (status != B_OK) {
		TRACE("TreeIterator::Traverse() Find failed\n");
		return B_ENTRY_NOT_FOUND;
	}

	return B_OK;
}


/*!	just sets the current key in the iterator.
*/
status_t
TreeIterator::Find(btrfs_key& key)
{
	if (fTree == NULL)
		return B_INTERRUPTED;
	fCurrentKey = key;
	return B_OK;
}


void
TreeIterator::Stop()
{
	fTree = NULL;
}
