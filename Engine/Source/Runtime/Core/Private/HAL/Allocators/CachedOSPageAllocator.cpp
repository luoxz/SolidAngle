#include "HAL/Allocators/CachedOSPageAllocator.h"
#include "HAL/SolidAngleMemory.h"
#include "Logging/LogMacros.h"
#include "CoreGlobals.h"


void* YCachedOSPageAllocator::AllocateImpl(SIZE_T Size, FFreePageBlock* First, FFreePageBlock* Last, uint32& FreedPageBlocksNum, uint32& CachedTotal)
{
	if (First != Last)
	{
		FFreePageBlock* Found = nullptr;

		for (FFreePageBlock* Block = First; Block != Last; ++Block)
		{
			// look for exact matches first, these are aligned to the page size, so it should be quite common to hit these on small pages sizes
			if (Block->ByteSize == Size)
			{
				Found = Block;
				break;
			}
		}

		if (!Found)
		{
			SIZE_T SizeTimes4 = Size * 4;

			for (FFreePageBlock* Block = First; Block != Last; ++Block)
			{
				// is it possible (and worth i.e. <25% overhead) to use this block
				if (Block->ByteSize >= Size && Block->ByteSize * 3 <= SizeTimes4)
				{
					Found = Block;
					break;
				}
			}
		}

		if (Found)
		{
			void* Result = Found->Ptr;
			UE_CLOG(!Result, LogMemory, Fatal, TEXT("OS memory allocation cache has been corrupted!"));
			CachedTotal -= Found->ByteSize;
			if (Found + 1 != Last)
			{
				YMemory::Memmove(Found, Found + 1, sizeof(FFreePageBlock) * ((Last - Found) - 1));
			}
			--FreedPageBlocksNum;
			return Result;
		}

		if (void* Ptr = YPlatformMemory::BinnedAllocFromOS(Size))
		{
			return Ptr;
		}

		// Are we holding on to much mem? Release it all.
		for (FFreePageBlock* Block = First; Block != Last; ++Block)
		{
			YPlatformMemory::BinnedFreeToOS(Block->Ptr, Block->ByteSize);
			Block->Ptr      = nullptr;
			Block->ByteSize = 0;
		}
		FreedPageBlocksNum = 0;
		CachedTotal        = 0;
	}

	return YPlatformMemory::BinnedAllocFromOS(Size);
}

void YCachedOSPageAllocator::FreeImpl(void* Ptr, SIZE_T Size, uint32 NumCacheBlocks, uint32 CachedByteLimit, FFreePageBlock* First, uint32& FreedPageBlocksNum, uint32& CachedTotal)
{
	if (Size > CachedByteLimit / 4)
	{
		YPlatformMemory::BinnedFreeToOS(Ptr, Size);
		return;
	}

	while (FreedPageBlocksNum && (FreedPageBlocksNum >= NumCacheBlocks || CachedTotal + Size > CachedByteLimit))
	{
		//Remove the oldest one
		void* FreePtr = First->Ptr;
		SIZE_T FreeSize = First->ByteSize;
		CachedTotal -= FreeSize;
		FreedPageBlocksNum--;
		if (FreedPageBlocksNum)
		{
			YMemory::Memmove(First, First + 1, sizeof(FFreePageBlock) * FreedPageBlocksNum);
		}
		YPlatformMemory::BinnedFreeToOS(FreePtr, FreeSize);
	}

	First[FreedPageBlocksNum].Ptr      = Ptr;
	First[FreedPageBlocksNum].ByteSize = Size;

	CachedTotal += Size;
	++FreedPageBlocksNum;
}

void YCachedOSPageAllocator::FreeAllImpl(FFreePageBlock* First, uint32& FreedPageBlocksNum, uint32& CachedTotal)
{
	while (FreedPageBlocksNum)
	{
		//Remove the oldest one
		void* FreePtr = First->Ptr;
		SIZE_T FreeSize = First->ByteSize;
		CachedTotal -= FreeSize;
		FreedPageBlocksNum--;
		if (FreedPageBlocksNum)
		{
			YMemory::Memmove(First, First + 1, sizeof(FFreePageBlock)* FreedPageBlocksNum);
		}
		YPlatformMemory::BinnedFreeToOS(FreePtr, FreeSize);
	}
}
