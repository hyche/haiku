/*
 * Copyright 2017, Chế Vũ Gia Hy, cvghy116@gmail.com.
 * Copyright 2011, Jérôme Duval, korli@users.berlios.de.
 * Copyright 2001-2010, Axel Dörfler, axeld@pinc-software.de.
 * This file may be used under the terms of the MIT License.
 */
#ifndef B_TREE_H
#define B_TREE_H


#include "btrfs.h"


#define BTREE_NULL			-1LL
#define BTREE_FREE			-2LL

#define BTRFS_MAX_TREE_DEPTH		8


enum btree_traversing {
	BTREE_FORWARD = 1,
	BTREE_EXACT = 0,
	BTREE_BACKWARD = -1,

	BTREE_BEGIN = 0,
	BTREE_END = -1
};


class Volume;
class Transaction;


//	#pragma mark - in-memory structures


template<class T> class Stack;
class TreeIterator;


// needed for searching (utilizing a stack)
struct node_and_key {
	off_t	nodeOffset;
	uint16	keyIndex;
};


class BTree {
public:
	class Path;
	class Node;

public:
								BTree(Volume* volume);
								BTree(Volume* volume,
									btrfs_stream* stream);
								BTree(Volume* volume,
									fsblock_t rootBlock);
								~BTree();

			status_t			FindExact(Path* path, btrfs_key& key,
									void** _value, uint32* _size = NULL,
									uint32* _offset = NULL) const;
			status_t			FindNext(Path* path, btrfs_key& key,
									void** _value, uint32* _size = NULL,
									uint32* _offset = NULL) const;
			status_t			FindPrevious(Path* path, btrfs_key& key,
									void** _value, uint32* _size = NULL,
									uint32* _offset = NULL) const;

			status_t			Traverse(btree_traversing type, Path* path,
									const btrfs_key& key) const;

			status_t			SwitchBranch(Path* path, Path** other,
									int level, int slot) const;
			status_t			PreviousLeaf(Path* path) const;
			status_t			NextLeaf(Path* path) const;
			status_t			SplitNode(Transaction& transaction,
									Path* path, int level);
			status_t			MakeEntries(Transaction& transaction,
									Path* path, const btrfs_key& startKey,
									int num, int length);
			status_t			InsertEntries(Transaction& transaction,
									Path* path, btrfs_entry* entries,
									void** data, int num);
			status_t			RemoveEntries(Transaction& transaction,
									Path* path, const btrfs_key& startKey,
									void** _data, int num);

			Volume*				SystemVolume() const { return fVolume; }
			status_t			SetRoot(off_t logical, fsblock_t* block);
			void				SetRoot(Node* root);
			fsblock_t			RootBlock() const { return fRootBlock; }
			off_t				LogicalRoot() const { return fLogicalRoot; }
			uint8				RootLevel() const { return fRootLevel; }

private:
								BTree(const BTree& other);
								BTree& operator=(const BTree& other);
									// no implementation

			status_t			_Find(Path* path, btrfs_key& key,
									void** _value, uint32* _size,
									uint32* _offset, btree_traversing type)
									const;
			void				_AddIterator(TreeIterator* iterator);
			void				_RemoveIterator(TreeIterator* iterator);
private:
			friend class TreeIterator;

			fsblock_t			fRootBlock;
			off_t				fLogicalRoot;
			uint8				fRootLevel;
			Volume*				fVolume;
			mutex				fIteratorLock;
			SinglyLinkedList<TreeIterator> fIterators;

public:
	class Node {
	public:
		Node(Volume* volume);
		Node(Volume* volume, off_t block);
		~Node();

			// just return from Header
		uint8*	FSID() const;
		uint64	LogicalAddress() const;
		uint64	Flags() const;
		uint8*	ChunkTreeUUID() const;
		uint64	Generation() const;
		uint64	Owner() const;
		uint32	ItemCount() const;
		uint8	Level() const;
		void	SetFSID(uint8* fsid);
		void	SetLogicalAddress(uint64 logicalAddress);
		void	SetFlags(uint64 flags);
		void	SetChunkTreeUUID(uint8* uuid);
		void	SetGeneration(uint64 generation);
		void	SetOwner(uint64 owner);
		void	SetItemCount(uint32 itemCount);
		void	SetLevel(uint8 level);

		btrfs_index*	Index(int i) const;

		btrfs_entry*	Item(int i) const;
		uint8*	ItemData(int i) const;

		void	Keep();
		void	Unset();

		void	SetTo(fsblock_t block);
		void	SetToWritable(fsblock_t block, int32 transactionId, bool empty);

		fsblock_t	BlockNum() const;
		bool	IsWritable() const;

		void	CalculateChecksum(bool verify = false);
		int		SpaceUsed() const;
		int		SpaceLeft() const;

		status_t	Copy(const Node* origin, uint32* slots, uint32* length,
						int num) const;
		status_t	Copy(const Node* origin, uint32 start, uint32 end,
						int length) const;
		status_t	MoveEntries(uint32 start, uint32 end, int length) const;

		status_t	SearchSlot(const btrfs_key& key, int* slot,
						btree_traversing type) const;
		void	Info() const;
	private:
		Node(const Node&);
		Node& operator=(const Node&);
			//no implementation

		void	_Copy(const Node* origin, uint32 at, uint32 from, uint32 to,
					int length) const;
		status_t	_SpaceCheck(int length) const;
		int		_CalculateSpace(uint32 from, uint32 to, uint8 type = 1) const;

		btrfs_stream*		fNode;
		Volume*				fVolume;
		fsblock_t			fBlockNumber;
		int					fEntrySize;
		bool				fWritable;
	};


	class Path {
	public:
		Path(BTree* tree);
		~Path();

		Node*		GetNode(int level, int* _slot = NULL) const;
		Node*		SetNode(off_t block, int slot);
		Node*		SetNode(const Node* node, int slot);
		status_t	GetCurrentEntry(btrfs_key* _key, void** _value,
						uint32* _size = NULL, uint32* _offset = NULL);
		status_t	GetEntry(int slot, btrfs_key* _key, void** _value,
						uint32* _size = NULL, uint32* _offset = NULL);
		status_t	SetCurrentEntry(const btrfs_key& key, void* value,
						uint32 size, uint32 offset);
		status_t	SetEntry(int slot, const btrfs_key& key, void* value,
						uint32 size, uint32 offset);
		status_t	SetEntry(int slot, const btrfs_entry& entry, void* value);

		int			Move(int level, int step);

		status_t	CopyOnWrite(Transaction& transaction, int level,
						uint32 start, int num, int length);
		status_t	InternalCopy(Transaction& transaction, int level);

		BTree*		Tree() const { return fTree; }
	private:
		Path(const Path&);
		Path operator=(const Path&);
	private:
		Node*	fNodes[BTRFS_MAX_TREE_DEPTH];
		int		fSlots[BTRFS_MAX_TREE_DEPTH];
		BTree*	fTree;
	};

};	// class BTree


class TreeIterator : public SinglyLinkedListLinkImpl<TreeIterator> {
public:
								TreeIterator(BTree* tree, const btrfs_key& key);
								TreeIterator(BTree::Path* path,
									const btrfs_key& key);
								~TreeIterator();

			void				Rewind(bool inverse = false);
			status_t			Find(const btrfs_key& key);
			status_t			GetNextEntry(void** _value,
									uint32* _size = NULL,
									uint32* _offset = NULL);
			status_t			GetPreviousEntry(void** _value,
									uint32* _size = NULL,
									uint32* _offset = NULL);

			BTree*				Tree() const { return fTree; }
			btrfs_key			Key() const { return fKey; }

private:
			friend class BTree;

			status_t			_Traverse(btree_traversing direction);
			status_t			_Find(btree_traversing type, btrfs_key& key,
									void** _value);
			status_t			_GetEntry(btree_traversing type, void** _value,
									uint32* _size, uint32* _offset);
			// called by BTree
			void				Stop();

private:
			BTree*			fTree;
			BTree::Path*	fPath;
			btrfs_key		fKey;
			status_t		fIteratorStatus;
};


//	#pragma mark - BTree::Node inline functions


inline uint8*
BTree::Node::FSID() const
{
	return fNode->header.fsid;
}


inline uint64
BTree::Node::LogicalAddress() const
{
	return fNode->header.LogicalAddress();
}


inline uint64
BTree::Node::Flags() const
{
	return fNode->header.Flags();
}


inline uint8*
BTree::Node::ChunkTreeUUID() const
{
	return fNode->header.chunk_tree_uuid;
}


inline uint64
BTree::Node::Generation() const
{
	return fNode->header.Generation();
}


inline uint64
BTree::Node::Owner() const
{
	return fNode->header.Owner();
}


inline uint32
BTree::Node::ItemCount() const
{
	return fNode->header.ItemCount();
}


inline uint8
BTree::Node::Level() const
{
	return fNode->header.Level();
}


inline void
BTree::Node::SetFSID(uint8* fsid)
{
	fNode->header.SetFSID(fsid);
}


inline void
BTree::Node::SetLogicalAddress(uint64 logicalAddress)
{
	fNode->header.SetLogicalAddress(logicalAddress);
}


inline void
BTree::Node::SetFlags(uint64 flags)
{
	fNode->header.SetFlags(flags);
}


inline void
BTree::Node::SetChunkTreeUUID(uint8* uuid)
{
	fNode->header.SetChunkTreeUUID(uuid);
}


inline void
BTree::Node::SetGeneration(uint64 generation)
{
	fNode->header.SetGeneration(generation);
}


inline void
BTree::Node::SetOwner(uint64 owner)
{
	fNode->header.SetOwner(owner);
}


inline void
BTree::Node::SetItemCount(uint32 itemCount)
{
	fNode->header.SetItemCount(itemCount);
}


inline void
BTree::Node::SetLevel(uint8 level)
{
	fNode->header.level = level;
}


inline btrfs_index*
BTree::Node::Index(int i) const
{
	return &fNode->index[i];
}


inline btrfs_entry*
BTree::Node::Item(int i) const
{
	return &fNode->entries[i];
}


inline uint8*
BTree::Node::ItemData(int i) const
{
	return (uint8*)Item(0) + Item(i)->Offset();
}


inline fsblock_t
BTree::Node::BlockNum() const
{
	return fBlockNumber;
}


inline bool
BTree::Node::IsWritable() const
{
	return fWritable;
}


//	#pragma mark - BTree::Path inline functions


inline status_t
BTree::Path::GetCurrentEntry(btrfs_key* _key, void** _value, uint32* _size,
	uint32* _offset)
{
	return GetEntry(fSlots[0], _key, _value, _size, _offset);
}


inline status_t
BTree::Path::SetCurrentEntry(const btrfs_key& key, void* value, uint32 size,
	uint32 offset)
{
	return SetEntry(fSlots[0], key, value, size, offset);
}


//	#pragma mark - TreeIterator inline functions


inline status_t
TreeIterator::GetNextEntry(void** _value, uint32* _size, uint32* _offset)
{
	return _GetEntry(BTREE_FORWARD, _value, _size, _offset);
}


inline status_t
TreeIterator::GetPreviousEntry(void** _value, uint32* _size, uint32* _offset)
{
	return _GetEntry(BTREE_BACKWARD, _value, _size, _offset);
}


#endif	// B_TREE_H
