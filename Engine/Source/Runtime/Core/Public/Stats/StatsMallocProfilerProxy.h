// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#if STATS

/**
 * Malloc proxy for gathering memory messages.
 */
class FStatsMallocProfilerProxy : public FMalloc
{
private:
	/** Malloc we're based on. */
	FMalloc* UsedMalloc;
	
	/** Whether the stats malloc profiler is enabled, disabled by default */
	bool bEnabled;

	FThreadSafeCounter AllocPtrCalls;
	FThreadSafeCounter FreePtrCalls;

public:
	/**
	 * Default constructor
	 * 
	 * @param	InMalloc - FMalloc that is going to be used for actual allocations
	 */
	FStatsMallocProfilerProxy( FMalloc* InMalloc);

	static CORE_API FStatsMallocProfilerProxy* Get();

	void SetState( bool bEnable );

	bool GetState() const
	{
		return bEnabled;
	}

	virtual void InitializeStatsMetadata() override;

	/**
	 * Tracks malloc operation.
	 *
	 * @param	Ptr	- Allocated pointer 
	 * @param	Size- Size of allocated pointer
	 */
	void TrackAlloc( void* Ptr, int64 Size );

	/**
	 * Tracks free operation
	 *
	 * @param	Ptr	- Freed pointer
	 */
	void TrackFree( void* Ptr );

	virtual void* Malloc( SIZE_T Size, uint32 Alignment ) override;
	virtual void* Realloc( void* OldPtr, SIZE_T NewSize, uint32 Alignment ) override;
	virtual void Free( void* Ptr ) override;

	virtual bool IsInternallyThreadSafe() const override
	{ 
		return UsedMalloc->IsInternallyThreadSafe(); 
	}

	virtual void UpdateStats() override;

	virtual void GetAllocatorStats( FGenericMemoryStats& out_Stats ) override
	{
		UsedMalloc->GetAllocatorStats( out_Stats );
	}

	virtual void DumpAllocatorStats( class FOutputDevice& Ar ) override
	{
		UsedMalloc->DumpAllocatorStats( Ar );
	}

	virtual bool ValidateHeap() override
	{
		return UsedMalloc->ValidateHeap();
	}

	virtual bool Exec( UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar ) override
	{
		return UsedMalloc->Exec( InWorld, Cmd, Ar);
	}

	virtual bool GetAllocationSize(void *Original, SIZE_T &SizeOut) override
	{
		return UsedMalloc->GetAllocationSize(Original,SizeOut);
	}

	virtual const TCHAR* GetDescriptiveName() override
	{ 
		return UsedMalloc->GetDescriptiveName(); 
	}
};


#endif //STATS