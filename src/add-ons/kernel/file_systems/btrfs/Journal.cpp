/*
 * Copyright 2017, Chế Vũ Gia Hy, cvghy116@gmail.com.
 * Copyright 2001-2012, Axel Dörfler, axeld@pinc-software.de.
 * This file may be used under the terms of the MIT License.
 */


#include "Journal.h"


//#define TRACE_BTRFS
#ifdef TRACE_BTRFS
#	define TRACE(x...) dprintf("\33[34mbtrfs:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif
#	define ERROR(x...) dprintf("\33[34mbtrfs:\33[0m " x)


Journal::Journal(Volume* volume)
	:
	fVolume(volume),
	fOwner(NULL),
	fTransactionID(0),
	fCurrentGeneration(volume->SuperBlock().Generation())
{
	recursive_lock_init(&fLock, "btrfs journal");
}


Journal::~Journal()
{
	recursive_lock_destroy(&fLock);
}


/*static*/ void
Journal::_TransactionEnded(int32 transactionID, int32 event, void* _journal)
{
	TRACE("TRANSACTION ENDED id %i\n", transactionID);
}


/*static*/ void
Journal::_TransactionWritten(int32 transactionID, int32 event, void* _journal)
{
	// increase transactionID of system
	TRACE("TRANSACTION WRITTEN id %i\n", transactionID);
	Journal* journal = (Journal*)_journal;

	//update superblock
	btrfs_super_block& superBlock = journal->GetVolume()->SuperBlock();

}


status_t
Journal::_TransactionDone(bool success)
{
	if (!success) {
		//if (fHasSubTransaction == true) {
		//	cache_abort_sub_transaction(fVolume->BlockCache(), fTransactionID);
		// We can continue to use the parent transaction afterwards
		//} else {
		cache_abort_transaction(fVolume->BlockCache(), fTransactionID);
		//	fUnwrittenTransactions = 0;
		//}
		return B_OK;
	}
	cache_end_transaction(fVolume->BlockCache(), fTransactionID,
		&_TransactionWritten, this);
	//cache_sync_transaction(fVolume->BlockCache(), fTransactionID);
	return B_OK;
}


status_t
Journal::Lock(Transaction* owner)
{
	status_t status = recursive_lock_lock(&fLock);
	if (status != B_OK)
		return status;
	if (recursive_lock_get_recursion(&fLock) > 1) {
		// we'll just use the current transaction again
		return B_OK;
	}

	//if (separateSubTransactions == true)
	//	fSeparateSubTransactions = true;

	if (owner != NULL)
		owner->SetParent(fOwner);

	fOwner = owner;

	if (fOwner != NULL) {
		//if (fUnwrittenTransactions > 0) {
			// start a sub transaction
		//	kprintf("start sub %i\n", fUnwrittenTransactions);
		//	cache_start_sub_transaction(fVolume->BlockCache(), fTransactionID);
		//	fHasSubTransaction = true;
		//} else
		fTransactionID = cache_start_transaction(fVolume->BlockCache());

		if (fTransactionID < B_OK) {
			recursive_lock_unlock(&fLock);
			return fTransactionID;
		}
		fCurrentGeneration++;
		TRACE("Journal::Lock() start transaction id: %i\n", fTransactionID);

		//cache_add_transaction_listener(fVolume->BlockCache(), fTransactionID,
		//	TRANSACTION_ENDED, &_TransactionEnded, this);
	}

	return B_OK;
}


status_t
Journal::UnLock(Transaction* owner, bool success)
{
	if (recursive_lock_get_recursion(&fLock) == 1) {
		if (owner != NULL) {
			status_t status = _TransactionDone(success);
			if (status != B_OK)
				return status;
			fOwner = owner->Parent();
		} else {
			fOwner = NULL;
		}
	}
	recursive_lock_unlock(&fLock);
	return B_OK;
}


// Transaction


Transaction::Transaction(Volume* volume)
	:
	fJournal(NULL),
	fParent(NULL)
	//fListeners(NULL),
{
	Start(volume);
}


Transaction::Transaction()
	:
	fJournal(NULL),
	fParent(NULL)
{
}


Transaction::~Transaction()
{
	if (fJournal != NULL) {
		fJournal->UnLock(this, false);
	}
}


bool
Transaction::HasBlock(fsblock_t blockNumber) const
{
	return cache_has_block_in_transaction(fJournal->GetVolume()->BlockCache(),
		ID(), blockNumber);
}


void
Transaction::InsertBlock(fsblock_t blockNumber)
{
	//fBlocksTable.insert(blockNumber);
}


status_t
Transaction::Start(Volume* volume)
{
	if (fJournal != NULL)
		return B_OK;

	fJournal = volume->GetJournal();
	if (fJournal != NULL && fJournal->Lock(this) == B_OK) {
		return B_OK;
	}
	fJournal = NULL;
	return B_ERROR;
}


status_t
Transaction::Done()
{
	status_t status = B_OK;
	if (fJournal != NULL) {
		status = fJournal->UnLock(this, true);
		if (status == B_OK)
			fJournal = NULL;
	}
	return status;
}
