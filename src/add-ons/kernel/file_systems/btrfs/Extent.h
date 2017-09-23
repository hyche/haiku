#ifndef EXTENT_H
#define EXTENT_H


#include "btrfs.h"
#include "Volume.h"


//#define TRACE_BTRFS
#ifdef TRACE_BTRFS
#	define TRACE(x...) dprintf("\33[34mbtrfs:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif


class ExtentController {
public:
						ExtentController();
						~ExtentController();
private:
						ExtentController(const ExtentController&);
						ExtentController& operator=(const ExtentController&);
};


#endif	// EXTENT_H
