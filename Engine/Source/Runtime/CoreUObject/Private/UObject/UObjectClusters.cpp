// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UObjectClusters.cpp: Unreal UObject Cluster helper functions
=============================================================================*/

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"
#include "Containers/LockFreeList.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectArray.h"
#include "UObject/UObjectBaseUtility.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectIterator.h"
#include "UObject/ReferenceChainSearch.h"
#include "UObject/LinkerLoad.h"
#include "UObject/FastReferenceCollector.h"

int32 GCreateGCClusters = 1;
static FAutoConsoleVariableRef CCreateGCClusters(
	TEXT("gc.CreateGCClusters"),
	GCreateGCClusters,
	TEXT("If true, the engine will attempt to create clusters of objects for better garbage collection performance."),
	ECVF_Default
	);

int32 GMergeGCClusters = 0;
static FAutoConsoleVariableRef CMergeGCClusters(
	TEXT("gc.MergeGCClusters"),
	GMergeGCClusters,
	TEXT("If true, when creating clusters, the clusters referenced from another cluster will get merged into one big cluster."),
	ECVF_Default
	);


FUObjectClusterContainer::FUObjectClusterContainer()
	: NumAllocatedClusters(0)
	, bClustersNeedDissolving(false)
{

}

int32 FUObjectClusterContainer::AllocateCluster(int32 InRootObjectIndex)
{
	int32 ClusterIndex = INDEX_NONE;
	if (FreeClusterIndices.Num())
	{
		ClusterIndex = FreeClusterIndices.Pop(false);
	}
	else
	{
		ClusterIndex = Clusters.Add(FUObjectCluster());
	}
	FUObjectCluster& NewCluster = Clusters[ClusterIndex];
	check(NewCluster.RootIndex == INDEX_NONE);
	NewCluster.RootIndex = InRootObjectIndex;
	NumAllocatedClusters++;
	return ClusterIndex;
}

void FUObjectClusterContainer::FreeCluster(int32 InClusterIndex)
{
	FUObjectCluster& Cluster = Clusters[InClusterIndex];
	check(Cluster.RootIndex != INDEX_NONE);
	FUObjectItem* RootItem = GUObjectArray.IndexToObject(Cluster.RootIndex);
	check(RootItem->GetClusterIndex() == InClusterIndex);
	RootItem->SetOwnerIndex(0);
	RootItem->ClearFlags(EInternalObjectFlags::ClusterRoot);

	for (int32 ReferencedClusterRootIndex : Cluster.ReferencedClusters)
	{
		if (ReferencedClusterRootIndex >= 0)
		{
			FUObjectItem* ReferencedClusterRootItem = GUObjectArray.IndexToObjectUnsafeForGC(ReferencedClusterRootIndex);
			if (ReferencedClusterRootItem->GetOwnerIndex() < 0)
			{
				FUObjectCluster& ReferencedCluster = Clusters[ReferencedClusterRootItem->GetClusterIndex()];
				ReferencedCluster.ReferencedByClusters.Remove(Cluster.RootIndex);
			}
		}
	}

	Cluster.RootIndex = INDEX_NONE;
	Cluster.Objects.Reset();
	Cluster.MutableObjects.Reset();
	Cluster.ReferencedClusters.Reset();
	Cluster.ReferencedByClusters.Reset();
	Cluster.bNeedsDissolving = false;
	FreeClusterIndices.Add(InClusterIndex);
	NumAllocatedClusters--;
	check(NumAllocatedClusters >= 0);
}

void FUObjectClusterContainer::DissolveCluster(FUObjectCluster& Cluster)
{
	FUObjectItem* RootObjectItem = GUObjectArray.IndexToObjectUnsafeForGC(Cluster.RootIndex);

	// Unreachable or not, we won't need this array later
	TArray<int32> ReferencedByClusters = MoveTemp(Cluster.ReferencedByClusters);

	// Unreachable clusters will be removed by GC during BeginDestroy phase (unhashing)
	if (!RootObjectItem->IsUnreachable())
	{
		const int32 OldClusterIndex = RootObjectItem->GetClusterIndex();
		for (int32 ClusterObjectIndex : Cluster.Objects)
		{
			FUObjectItem* ClusterObjectItem = GUObjectArray.IndexToObjectUnsafeForGC(ClusterObjectIndex);
			ClusterObjectItem->SetOwnerIndex(0);
		}		

		FreeCluster(OldClusterIndex);
	}

	// Recursively dissolve all clusters this cluster is directly referenced by
	for (int32 ReferencedByClusterRootIndex : ReferencedByClusters)
	{
		FUObjectItem* ReferencedByClusterRootObjectItem = GUObjectArray.IndexToObjectUnsafeForGC(ReferencedByClusterRootIndex);
		if (ReferencedByClusterRootObjectItem->GetOwnerIndex())
		{
			DissolveCluster(Clusters[ReferencedByClusterRootObjectItem->GetClusterIndex()]);
		}
	}
}

void FUObjectClusterContainer::DissolveClusterAndMarkObjectsAsUnreachable(const int32 CurrentIndex, FUObjectItem* RootObjectItem)
{
	const int32 OldClusterIndex = RootObjectItem->GetClusterIndex();
	FUObjectCluster& Cluster = Clusters[OldClusterIndex];

	// Unreachable or not, we won't need this array later
	TArray<int32> ReferencedByClusters = MoveTemp(Cluster.ReferencedByClusters);	

	for (int32 ClusterObjectIndex : Cluster.Objects)
	{
		FUObjectItem* ClusterObjectItem = GUObjectArray.IndexToObjectUnsafeForGC(ClusterObjectIndex);
		ClusterObjectItem->SetOwnerIndex(0);
		if (ClusterObjectIndex < CurrentIndex)
		{
			ClusterObjectItem->SetFlags(EInternalObjectFlags::Unreachable | EInternalObjectFlags::NoStrongReference);
		}
	}

	UObject* ClusterRootObject = static_cast<UObject*>(RootObjectItem->Object);
	ClusterRootObject->OnClusterMarkedAsPendingKill();

	FreeCluster(OldClusterIndex);

	// Recursively dissolve all clusters this cluster is directly referenced by
	for (int32 ReferencedByClusterRootIndex : ReferencedByClusters)
	{
		FUObjectItem* ReferencedByClusterRootItem = GUObjectArray.IndexToObjectUnsafeForGC(ReferencedByClusterRootIndex);
		if (ReferencedByClusterRootItem->HasAnyFlags(EInternalObjectFlags::ClusterRoot))
		{
			if (ReferencedByClusterRootIndex < CurrentIndex)
			{
				ReferencedByClusterRootItem->SetFlags(EInternalObjectFlags::Unreachable | EInternalObjectFlags::NoStrongReference);
			}
			DissolveClusterAndMarkObjectsAsUnreachable(CurrentIndex, ReferencedByClusterRootItem);
		}
	}
}

void FUObjectClusterContainer::DissolveClusters()
{
	for (FUObjectCluster& Cluster : Clusters)
	{
		if (Cluster.RootIndex >= 0 && Cluster.bNeedsDissolving)
		{
			DissolveCluster(Cluster);
		}
	}
	bClustersNeedDissolving = false;
}

#if !UE_BUILD_SHIPPING

void DumpClusterToLog(const FUObjectCluster& Cluster, bool bHierarchy)
{
	FUObjectItem* RootItem = GUObjectArray.IndexToObjectUnsafeForGC(Cluster.RootIndex);
	UObject* RootObject = static_cast<UObject*>(RootItem->Object);
	UE_LOG(LogObj, Display, TEXT("%s (Index: %d), Size: %d, ReferencedClusters: %d"), *RootObject->GetFullName(), Cluster.RootIndex, Cluster.Objects.Num(), Cluster.ReferencedClusters.Num());
	if (bHierarchy)
	{
		int32 Index = 0;
		for (int32 ObjectIndex : Cluster.Objects)
		{
			FUObjectItem* ObjectItem = GUObjectArray.IndexToObjectUnsafeForGC(ObjectIndex);
			UObject* Object = static_cast<UObject*>(ObjectItem->Object);
			UE_LOG(LogObj, Display, TEXT("    [%.4d]: %s (Index: %d)"), Index++, *Object->GetFullName(), ObjectIndex);
		}
		UE_LOG(LogObj, Display, TEXT("  Referenced clusters: %d"), Cluster.ReferencedClusters.Num());
		for (int32 ClusterRootIndex : Cluster.ReferencedClusters)
		{
			if (ClusterRootIndex >= 0)
			{
				FUObjectItem* ClusterRootItem = GUObjectArray.IndexToObjectUnsafeForGC(ClusterRootIndex);
				UObject* ClusterRootObject = static_cast<UObject*>(ClusterRootItem->Object);
				UE_LOG(LogObj, Display, TEXT("    -> %s (Index: %d)"), *ClusterRootObject->GetFullName(), ClusterRootIndex);
			}
			else
			{
				UE_LOG(LogObj, Display, TEXT("    -> nullptr"));
			}
		}
		UE_LOG(LogObj, Display, TEXT("  External (mutable) objects: %d"), Cluster.MutableObjects.Num());
		for (int32 ObjectIndex : Cluster.MutableObjects)
		{
			if (ObjectIndex >= 0)
			{
				FUObjectItem* ObjectItem = GUObjectArray.IndexToObjectUnsafeForGC(ObjectIndex);
				UObject* Object = static_cast<UObject*>(ObjectItem->Object);
				UE_LOG(LogObj, Display, TEXT("    -> %s (Index: %d)"), *Object->GetFullName(), ObjectIndex);
			}
			else
			{
				UE_LOG(LogObj, Display, TEXT("    -> nullptr"));
			}
		}
	}
}


// Dumps all clusters to log.
static void ListClusters(const TArray<FString>& Args)
{
	const bool bHierarchy = Args.Num() && Args[0] == TEXT("Hierarchy");
	int32 MaxInterClusterReferences = 0;
	int32 TotalInterClusterReferences = 0;
	int32 MaxClusterSize = 0;
	int32 TotalClusterObjects = 0;	
	int32 NumClusters = 0;

	for (const FUObjectCluster& Cluster : GUObjectClusters.GetClustersUnsafe())
	{
		if (Cluster.RootIndex == INDEX_NONE)
		{
			continue;
		}

		NumClusters++;

		MaxInterClusterReferences = FMath::Max(MaxInterClusterReferences, Cluster.ReferencedClusters.Num());
		TotalInterClusterReferences += Cluster.ReferencedClusters.Num();
		MaxClusterSize = FMath::Max(MaxClusterSize, Cluster.Objects.Num());
		TotalClusterObjects += Cluster.Objects.Num();

		DumpClusterToLog(Cluster, bHierarchy);
	}
	UE_LOG(LogObj, Display, TEXT("Number of clusters: %d"), NumClusters);
	UE_LOG(LogObj, Display, TEXT("Maximum cluster size: %d"), MaxClusterSize);
	UE_LOG(LogObj, Display, TEXT("Average cluster size: %d"), NumClusters ? (TotalClusterObjects / NumClusters) : 0);
	UE_LOG(LogObj, Display, TEXT("Number of objects in GC clusters: %d"), TotalClusterObjects);
	UE_LOG(LogObj, Display, TEXT("Maximum number of custer-to-cluster references: %d"), MaxInterClusterReferences);
	UE_LOG(LogObj, Display, TEXT("Average number of custer-to-cluster references: %d"), NumClusters ? (TotalInterClusterReferences / NumClusters) : 0);
}

static void FindStaleClusters(const TArray<FString>& Args)
{
	// This is seriously slow.
	UE_LOG(LogObj, Display, TEXT("Searching for stale clusters. This may take a while...")); 
	int32 NumStaleClusters = 0;
	int32 TotalNumClusters = 0;
	for (FRawObjectIterator It(true); It; ++It)
	{
		FUObjectItem* ObjectItem = *It;
		if (ObjectItem->HasAnyFlags(EInternalObjectFlags::ClusterRoot))
		{
			TotalNumClusters++;

			UObject* ClusterRootObject = static_cast<UObject*>(ObjectItem->Object);
			FReferenceChainSearch SearchRefs(ClusterRootObject, FReferenceChainSearch::ESearchMode::Shortest);
			
			bool bReferenced = false;
			if (SearchRefs.GetReferenceChains().Num() > 0)
			{
				for (const FReferenceChainSearch::FReferenceChain& ReferenceChain : SearchRefs.GetReferenceChains())
				{
					UObject* ReferencingObj = ReferenceChain.RefChain[0].ReferencedBy;
					// Ignore internal references
					if (!ReferencingObj->IsIn(ClusterRootObject) && ReferencingObj != ClusterRootObject)
					{
						bReferenced = true;
						break;
					}
				}
			}
			if (!bReferenced)
			{
				NumStaleClusters++;
				UE_LOG(LogObj, Display, TEXT("Cluster %s has no external references:"), *ClusterRootObject->GetFullName());
				SearchRefs.PrintResults();
			}
		}
	}
	UE_LOG(LogObj, Display, TEXT("Found %d clusters, including %d stale."), TotalNumClusters, NumStaleClusters);
}

static FAutoConsoleCommand ListClustersCommand(
	TEXT("gc.ListClusters"),
	TEXT("Dumps all clusters do output log. When 'Hiearchy' argument is specified lists all objects inside clusters."),
	FConsoleCommandWithArgsDelegate::CreateStatic(ListClusters)
	);

static FAutoConsoleCommand FindStaleClustersCommand(
	TEXT("gc.FindStaleClusters"),
	TEXT("Dumps all clusters do output log that are not referenced by anything."),
	FConsoleCommandWithArgsDelegate::CreateStatic(FindStaleClusters)
	);

#endif // !UE_BUILD_SHIPPING

/**
* Pool for reducing allocations when constructing clusters
*/
class FClusterArrayPool
{
public:

	/**
	* Gets the singleton instance of the FObjectArrayPool
	* @return Pool singleton.
	*/
	FORCEINLINE static FClusterArrayPool& Get()
	{
		static FClusterArrayPool Singleton;
		return Singleton;
	}

	/**
	* Gets an event from the pool or creates one if necessary.
	*
	* @return The array.
	* @see ReturnToPool
	*/
	FORCEINLINE TArray<UObject*>* GetArrayFromPool()
	{
		TArray<UObject*>* Result = Pool.Pop();
		if (!Result)
		{
			Result = new TArray<UObject*>();
		}
		check(Result);
#if UE_BUILD_DEBUG
		NumberOfUsedArrays.Increment();
#endif // UE_BUILD_DEBUG
		return Result;
	}

	/**
	* Returns an array to the pool.
	*
	* @param Array The array to return.
	* @see GetArrayFromPool
	*/
	FORCEINLINE void ReturnToPool(TArray<UObject*>* Array)
	{
#if UE_BUILD_DEBUG
		const int32 CheckUsedArrays = NumberOfUsedArrays.Decrement();
		checkSlow(CheckUsedArrays >= 0);
#endif // UE_BUILD_DEBUG
		check(Array);
		Array->Reset();
		Pool.Push(Array);
	}

	/** Performs memory cleanup */
	void Cleanup()
	{
#if UE_BUILD_DEBUG
		const int32 CheckUsedArrays = NumberOfUsedArrays.GetValue();
		checkSlow(CheckUsedArrays == 0);
#endif // UE_BUILD_DEBUG

		uint32 FreedMemory = 0;
		TArray< TArray<UObject*>* > AllArrays;
		Pool.PopAll(AllArrays);
		for (TArray<UObject*>* Array : AllArrays)
		{
			FreedMemory += Array->GetAllocatedSize();
			delete Array;
		}
		UE_LOG(LogObj, Log, TEXT("Freed %ub from %d cluster array pools."), FreedMemory, AllArrays.Num());
	}

#if UE_BUILD_DEBUG
	void CheckLeaks()
	{
		// This function is called after cluster has been created so at this point there should be no
		// arrays used by cluster creation code and all should be returned to the pool
		const int32 LeakedPoolArrays = NumberOfUsedArrays.GetValue();
		checkSlow(LeakedPoolArrays == 0);
	}
#endif

private:

	/** Holds the collection of recycled arrays. */
	TLockFreePointerListLIFO< TArray<UObject*> > Pool;

#if UE_BUILD_DEBUG
	/** Number of arrays currently acquired from the pool by clusters */
	FThreadSafeCounter NumberOfUsedArrays;
#endif // UE_BUILD_DEBUG
};

/** Called on shutdown to free cluster memory */
void CleanupClusterArrayPools()
{
	FClusterArrayPool::Get().Cleanup();
}

/**
 * Handles UObject references found by TFastReferenceCollector
 */
class FClusterReferenceProcessor
{
	int32 ClusterRootIndex;
	FUObjectCluster& Cluster;
	volatile bool bIsRunningMultithreaded;
public:

	FClusterReferenceProcessor(int32 InClusterRootIndex, FUObjectCluster& InCluster)
		: ClusterRootIndex(InClusterRootIndex)
		, Cluster(InCluster)
		, bIsRunningMultithreaded(false)
	{}

	FORCEINLINE int32 GetMinDesiredObjectsPerSubTask() const
	{
		// We're not running the processor in parallel when creating clusters
		return 0;
	}

	FORCEINLINE volatile bool IsRunningMultithreaded() const
	{
		// This should always be false
		return bIsRunningMultithreaded;
	}

	FORCEINLINE void SetIsRunningMultithreaded(bool bIsParallel)
	{
		check(!bIsParallel);
		bIsRunningMultithreaded = bIsParallel;
	}

	void UpdateDetailedStats(UObject* CurrentObject, uint32 DeltaCycles)
	{
	}

	void LogDetailedStatsSummary()
	{
	}

	/**
	 * Adds an object to cluster (if possible)
	 *
	 * @param ObjectIndex UObject index in GUObjectArray
	 * @param ObjectItem UObject's entry in GUObjectArray
	 * @param Obj The object to add to cluster
	 * @param ObjectsToSerialize An array of remaining objects to serialize (Obj must be added to it if Obj can be added to cluster)
	 * @param bOuterAndClass If true, the Obj's Outer and Class will also be added to the cluster
	 */
	void AddObjectToCluster(int32 ObjectIndex, FUObjectItem* ObjectItem, UObject* Obj, TArray<UObject*>& ObjectsToSerialize, bool bOuterAndClass)
	{
		// If we haven't finished loading, we can't be sure we know all the references
		check(!Obj->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad));
		if (ObjectIndex != ClusterRootIndex && ObjectItem->GetOwnerIndex() == 0 && !ObjectItem->IsRootSet() && !GUObjectArray.IsDisregardForGC(Obj) && Obj->CanBeInCluster())
		{
			ObjectsToSerialize.Add(Obj);
			check(!ObjectItem->HasAnyFlags(EInternalObjectFlags::ClusterRoot));
			ObjectItem->SetOwnerIndex(ClusterRootIndex);
			Cluster.Objects.Add(ObjectIndex);

			if (bOuterAndClass)
			{
				UObject* ObjOuter = Obj->GetOuter();
				if (ObjOuter)
				{
					int32 OuterIndex = GUObjectArray.ObjectToIndex(ObjOuter);
					AddObjectToCluster(OuterIndex, GUObjectArray.IndexToObjectUnsafeForGC(OuterIndex), ObjOuter, ObjectsToSerialize, false);
				}
				if (!Obj->GetClass()->HasAllClassFlags(CLASS_Native))
				{
					UObject* ObjectClass = Obj->GetClass();
					HandleTokenStreamObjectReference(ObjectsToSerialize, Obj, ObjectClass, INDEX_NONE, true);
					UObject* ObjectClassOuter = Obj->GetClass()->GetOuter();
					HandleTokenStreamObjectReference(ObjectsToSerialize, Obj, ObjectClassOuter, INDEX_NONE, true);
				}
			}
		}		
	}

	/**
	* Merges an existing cluster with the currently constructed one
	*
	* @param ObjectItem UObject's entry in GUObjectArray. This is either the other cluster root or one if the cluster objects.
	* @param Obj The object to add to cluster
	* @param ObjectsToSerialize An array of remaining objects to serialize (Obj must be added to it if Obj can be added to cluster)
	*/
	void MergeCluster(FUObjectItem* ObjectItem, UObject* Object, TArray<UObject*>& ObjectsToSerialize)
	{
		FUObjectItem* OtherClusterRootItem = nullptr;
		int32 OtherClusterRootIndex = INDEX_NONE;
		// First find the other cluster root item and its index
		if (ObjectItem->HasAnyFlags(EInternalObjectFlags::ClusterRoot))
		{
			OtherClusterRootIndex = GUObjectArray.ObjectToIndex(Object);
			OtherClusterRootItem = ObjectItem;
		}
		else
		{
			OtherClusterRootIndex = ObjectItem->GetOwnerIndex();
			check(OtherClusterRootIndex > 0);
			OtherClusterRootItem = GUObjectArray.IndexToObjectUnsafeForGC(OtherClusterRootIndex);
		}
		// This is another cluster, merge it with this one
		FUObjectCluster& ClusterToMerge = GUObjectClusters[OtherClusterRootItem->GetClusterIndex()];
		for (int32 OtherClusterObjectIndex : ClusterToMerge.Objects)
		{
			FUObjectItem* OtherClusterObjectItem = GUObjectArray.IndexToObjectUnsafeForGC(OtherClusterObjectIndex);
			OtherClusterObjectItem->SetOwnerIndex(0);
			AddObjectToCluster(OtherClusterObjectIndex, OtherClusterObjectItem, static_cast<UObject*>(OtherClusterObjectItem->Object), ObjectsToSerialize, true);
		}		
		GUObjectClusters.FreeCluster(OtherClusterRootItem->GetClusterIndex());

		// Make sure the root object is also added to the current cluster
		OtherClusterRootItem->ClearFlags(EInternalObjectFlags::ClusterRoot);
		OtherClusterRootItem->SetOwnerIndex(0);
		AddObjectToCluster(OtherClusterRootIndex, OtherClusterRootItem, static_cast<UObject*>(OtherClusterRootItem->Object), ObjectsToSerialize, true);

		// Sanity check so that we make sure the object was actually in the lister it said it belonged to
		check(ObjectItem->GetOwnerIndex() == ClusterRootIndex);
	}

	/**
	* Handles UObject reference from the token stream. Performance is critical here so we're FORCEINLINING this function.
	*
	* @param ObjectsToSerialize An array of remaining objects to serialize (Obj must be added to it if Obj can be added to cluster)
	* @param ReferencingObject Object referencing the object to process.
	* @param TokenIndex Index to the token stream where the reference was found.
	* @param bAllowReferenceElimination True if reference elimination is allowed (ignored when constructing clusters).
	*/
	FORCEINLINE void HandleTokenStreamObjectReference(TArray<UObject*>& ObjectsToSerialize, UObject* ReferencingObject, UObject*& Object, const int32 TokenIndex, bool bAllowReferenceElimination)
	{
		if (Object)
		{
			// If we haven't finished loading, we can't be sure we know all the references
			check(!Object->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad));

			FUObjectItem* ObjectItem = GUObjectArray.ObjectToObjectItem(Object);

			// Add encountered object reference to list of to be serialized objects if it hasn't already been added.
			if (ObjectItem->GetOwnerIndex() != ClusterRootIndex)
			{
				if (ObjectItem->HasAnyFlags(EInternalObjectFlags::ClusterRoot) || ObjectItem->GetOwnerIndex() != 0)
				{					
					if (GMergeGCClusters)
					{
						// This is an existing cluster, merge it with the current one.
						MergeCluster(ObjectItem, Object, ObjectsToSerialize);
					}
					else
					{
						// Simply reference this cluster and all clusters it's referencing
						const int32 OtherClusterRootIndex = ObjectItem->HasAnyFlags(EInternalObjectFlags::ClusterRoot) ? GUObjectArray.ObjectToIndex(Object) : ObjectItem->GetOwnerIndex();
						FUObjectItem* OtherClusterRootItem = GUObjectArray.IndexToObject(OtherClusterRootIndex);
						const int32 OtherClusterIndex = OtherClusterRootItem->GetClusterIndex();
						FUObjectCluster& OtherCluster = GUObjectClusters[OtherClusterIndex];
						Cluster.ReferencedClusters.AddUnique(OtherClusterRootIndex);
						OtherCluster.ReferencedByClusters.AddUnique(ClusterRootIndex);

						for (int32 OtherClusterReferencedCluster : OtherCluster.ReferencedClusters)
						{
							check(OtherClusterReferencedCluster != ClusterRootIndex);
							Cluster.ReferencedClusters.AddUnique(OtherClusterReferencedCluster);
						}
						for (int32 OtherClusterReferencedMutableObjectIndex : OtherCluster.MutableObjects)
						{
							Cluster.MutableObjects.AddUnique(OtherClusterReferencedMutableObjectIndex);
						}
					}
				}
				else if (ObjectItem->GetOwnerIndex() == 0 && !ObjectItem->IsRootSet() && !GUObjectArray.IsDisregardForGC(Object) &&
					!(Object->CanBeClusterRoot() && Object->HasAnyFlags(RF_NeedLoad|RF_NeedPostLoad))) // Objects that can create clusters themselves and haven't been postloaded yet should be excluded
				{
					// New object, add it to the cluster.
					if (Object->CanBeInCluster())
					{
						AddObjectToCluster(GUObjectArray.ObjectToIndex(Object), ObjectItem, Object, ObjectsToSerialize, true);
					}
					else
					{
						Cluster.MutableObjects.AddUnique(GUObjectArray.ObjectToIndex(Object));
					}
				}
			}
		}
	}
};

/**
 * Specialized FReferenceCollector that uses FClusterReferenceProcessor to construct the cluster
 */
template <class TProcessor>
class TClusterCollector : public FReferenceCollector
{
	TProcessor& Processor;
	TArray<UObject*>& ObjectArray;

public:
	TClusterCollector(TProcessor& InProcessor, TArray<UObject*>& InObjectArray)
		: Processor(InProcessor)
		, ObjectArray(InObjectArray)
	{
	}
	virtual void HandleObjectReference(UObject*& Object, const UObject* ReferencingObject, const UProperty* ReferencingProperty) override
	{
		Processor.HandleTokenStreamObjectReference(ObjectArray, const_cast<UObject*>(ReferencingObject), Object, INDEX_NONE, false);
	}
	virtual void HandleObjectReferences(UObject** InObjects, const int32 ObjectNum, const UObject* ReferencingObject, const UProperty* InReferencingProperty) override
	{
		for (int32 ObjectIndex = 0; ObjectIndex < ObjectNum; ++ObjectIndex)
		{
			UObject*& Object = InObjects[ObjectIndex];
			Processor.HandleTokenStreamObjectReference(ObjectArray, const_cast<UObject*>(ReferencingObject), Object, INDEX_NONE, false);
		}
	}
	virtual bool IsIgnoringArchetypeRef() const override
	{
		return false;
	}
	virtual bool IsIgnoringTransient() const override
	{
		return false;
	}
};

/** Looks through objects loaded with a package and creates clusters from them */
void CreateClustersFromPackage(FLinkerLoad* PackageLinker)
{	
	if (FPlatformProperties::RequiresCookedData() && !GIsInitialLoad && GCreateGCClusters && !GUObjectArray.IsOpenForDisregardForGC())
	{
		check(PackageLinker);

		for (FObjectExport& Export : PackageLinker->ExportMap)
		{
			if (Export.Object && Export.Object->CanBeClusterRoot())
			{
				Export.Object->CreateCluster();
			}
		}
	}
}

void UObjectBaseUtility::AddToCluster(UObjectBaseUtility* ClusterRootOrObjectFromCluster, bool bAddAsMutableObject /* = false */)
{
	check(ClusterRootOrObjectFromCluster);

	const int32 OuterIndex = GUObjectArray.ObjectToIndex(ClusterRootOrObjectFromCluster);
	FUObjectItem* OuterItem = GUObjectArray.IndexToObjectUnsafeForGC(OuterIndex);
	int32 ClusterRootIndex = 0;
	if (OuterItem->HasAnyFlags(EInternalObjectFlags::ClusterRoot))
	{
		ClusterRootIndex = OuterIndex;
	}
	else
	{
		ClusterRootIndex = OuterItem->GetOwnerIndex();
	}
	if (ClusterRootIndex != 0)
	{
		const int32 ClusterIndex = ClusterRootIndex > 0 ? GUObjectArray.IndexToObject(ClusterRootIndex)->GetClusterIndex() : OuterItem->GetClusterIndex();
		FUObjectCluster& Cluster = GUObjectClusters[ClusterIndex];
		if (!bAddAsMutableObject)
		{
			FClusterReferenceProcessor Processor(ClusterRootIndex, Cluster);
			TFastReferenceCollector<false, FClusterReferenceProcessor, TClusterCollector<FClusterReferenceProcessor>, FClusterArrayPool, true> ReferenceCollector(Processor, FClusterArrayPool::Get());
			TArray<UObject*> ObjectsToProcess;
			UObject* ThisObject = static_cast<UObject*>(this);
			Processor.HandleTokenStreamObjectReference(ObjectsToProcess, static_cast<UObject*>(ClusterRootOrObjectFromCluster), ThisObject, INDEX_NONE, true);
			if (ObjectsToProcess.Num())
			{
				ReferenceCollector.CollectReferences(ObjectsToProcess);
			}
		}
		else
		{
			// Adds this object's index to the MutableObjects array keeping it sorted and unique
			const int32 ThisObjectIndex = GUObjectArray.ObjectToIndex(this);
			int32 InsertedAt = INDEX_NONE;
			for (int32 MutableObjectIndex = 0; MutableObjectIndex < Cluster.MutableObjects.Num() && InsertedAt == INDEX_NONE; ++MutableObjectIndex)
			{
				if (Cluster.MutableObjects[MutableObjectIndex] > ThisObjectIndex)
				{
					InsertedAt = Cluster.MutableObjects.Insert(ThisObjectIndex, MutableObjectIndex);
				}
				else if (Cluster.MutableObjects[MutableObjectIndex] == ThisObjectIndex)
				{
					InsertedAt = MutableObjectIndex;
				}
			}
			if (InsertedAt == INDEX_NONE)
			{
				Cluster.MutableObjects.Add(ThisObjectIndex);
			}
		}
	}
}

bool UObjectBaseUtility::CanBeInCluster() const
{
	return true;
}

void UObjectBaseUtility::CreateCluster()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UObjectBaseUtility::CreateCluster"), STAT_FArchiveRealtimeGC_CreateCluster, STATGROUP_GC);

	FUObjectItem* RootItem = GUObjectArray.IndexToObject(InternalIndex);
	if (RootItem->GetOwnerIndex() != 0 || RootItem->HasAnyFlags(EInternalObjectFlags::ClusterRoot))
	{
		return;
	}

	// If we haven't finished loading, we can't be sure we know all the references
	check(!HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad));

	// Create a new cluster, reserve an arbitrary amount of memory for it.
	const int32 ClusterIndex = GUObjectClusters.AllocateCluster(InternalIndex);
	FUObjectCluster& Cluster = GUObjectClusters[ClusterIndex];
	Cluster.Objects.Reserve(64);

	// Collect all objects referenced by cluster root and by all objects it's referencing
	FClusterReferenceProcessor Processor(InternalIndex, Cluster);
	TFastReferenceCollector<false, FClusterReferenceProcessor, TClusterCollector<FClusterReferenceProcessor>, FClusterArrayPool, true> ReferenceCollector(Processor, FClusterArrayPool::Get());
	TArray<UObject*> ObjectsToProcess;
	ObjectsToProcess.Add(static_cast<UObject*>(this));
	ReferenceCollector.CollectReferences(ObjectsToProcess);
#if UE_BUILD_DEBUG
	FClusterArrayPool::Get().CheckLeaks();
#endif

	if (Cluster.Objects.Num())
	{
		// Add new cluster to the global cluster map.
		Cluster.Objects.Sort();
		Cluster.ReferencedClusters.Sort();		
		Cluster.MutableObjects.Sort();		
		check(RootItem->GetOwnerIndex() == 0);
		RootItem->SetClusterIndex(ClusterIndex);
		RootItem->SetFlags(EInternalObjectFlags::ClusterRoot);
	}
	else
	{
		check(RootItem->GetOwnerIndex() == 0);
		RootItem->SetClusterIndex(ClusterIndex);
		GUObjectClusters.FreeCluster(ClusterIndex);
	}
}


/**
* Handles UObject references found by TFastReferenceCollector
*/
class FClusterVerifyReferenceProcessor
{	
	const UObject* const ClusterRootObject;
	const int32 ClusterRootIndex;
	const FUObjectCluster& Cluster;
	volatile bool bIsRunningMultithreaded;
	bool bFailed;
	TSet<UObject*> ProcessedObjects;

public:

	FClusterVerifyReferenceProcessor(UObject* InClusterRootObject)		
		: ClusterRootObject(InClusterRootObject)
		, ClusterRootIndex(GUObjectArray.ObjectToIndex(InClusterRootObject))
		, Cluster(GUObjectClusters[GUObjectArray.IndexToObject(ClusterRootIndex)->GetClusterIndex()]) // This can't fail otherwise there's something wrong with cluster creation code
		, bIsRunningMultithreaded(false)
		, bFailed(false)
	{
	}

	bool NoExternalReferencesFound() const
	{
		return !bFailed;
	}

	FORCEINLINE int32 GetMinDesiredObjectsPerSubTask() const
	{
		// We're not running the processor in parallel when creating clusters
		return 0;
	}

	FORCEINLINE volatile bool IsRunningMultithreaded() const
	{
		// This should always be false
		return bIsRunningMultithreaded;
	}

	FORCEINLINE void SetIsRunningMultithreaded(bool bIsParallel)
	{
		check(!bIsParallel);
		bIsRunningMultithreaded = bIsParallel;
	}

	void UpdateDetailedStats(UObject* CurrentObject, uint32 DeltaCycles)
	{
	}

	void LogDetailedStatsSummary()
	{
	}

	/**
	* Handles UObject reference from the token stream. Performance is critical here so we're FORCEINLINING this function.
	*
	* @param ObjectsToSerialize An array of remaining objects to serialize (Obj must be added to it if Obj can be added to cluster)
	* @param ReferencingObject Object referencing the object to process.
	* @param TokenIndex Index to the token stream where the reference was found.
	* @param bAllowReferenceElimination True if reference elimination is allowed (ignored when constructing clusters).
	*/
	FORCEINLINE void HandleTokenStreamObjectReference(TArray<UObject*>& ObjectsToSerialize, UObject* ReferencingObject, UObject*& Object, const int32 TokenIndex, bool bAllowReferenceElimination)
	{
		if (Object)
		{
#if !(UE_BUILD_TEST || UE_BUILD_SHIPPING)
			if (
#if DO_POINTER_CHECKS_ON_GC
				!IsPossiblyAllocatedUObjectPointer(Object) ||
#endif
				!Object->IsValidLowLevelFast())
		{
			FString TokenDebugInfo;
			if (UClass *Class = (ReferencingObject ? ReferencingObject->GetClass() : nullptr))
			{
				const FTokenInfo& TokenInfo = Class->DebugTokenMap.GetTokenInfo(TokenIndex);
				TokenDebugInfo = FString::Printf(TEXT("ReferencingObjectClass: %s, Property Name: %s, Offset: %d"),
					*Class->GetFullName(), *TokenInfo.Name.GetPlainNameString(), TokenInfo.Offset);
			}
			else
			{
				// This means this objects is most likely being referenced by AddReferencedObjects
				TokenDebugInfo = TEXT("Native Reference");
			}

			TArray<FString> Args;
			Args.Add(TEXT("Hierarchy"));
			ListClusters(Args);

			UE_LOG(LogObj, Fatal, TEXT("Invalid object while verifying cluster assumptions: 0x%016llx, ReferencingObject: %s, %s, TokenIndex: %d"),
				(int64)(PTRINT)Object,
				ReferencingObject ? *ReferencingObject->GetFullName() : TEXT("NULL"),
				*TokenDebugInfo, TokenIndex);
		}
#endif
			if (!ProcessedObjects.Contains(Object))
		{
			ProcessedObjects.Add(Object);
			FUObjectItem* ObjectItem = GUObjectArray.ObjectToObjectItem(Object);
			if (ObjectItem->GetOwnerIndex() <= 0)
			{
				// We are allowed to reference other clusters, root set objects and objects from diregard for GC pool
					if (!ObjectItem->HasAnyFlags(EInternalObjectFlags::ClusterRoot|EInternalObjectFlags::RootSet) 
						&& !GUObjectArray.IsDisregardForGC(Object) && Object->CanBeInCluster() &&
						!Cluster.MutableObjects.Contains(GUObjectArray.ObjectToIndex(Object))) // This is for objects that had RF_NeedLoad|RF_NeedPostLoad set when creating the cluster
				{
					UE_LOG(LogObj, Warning, TEXT("Object 0x%016llx %s from cluster %s is referencing 0x%016llx %s which is not part of root set or cluster."),
						(int64)(PTRINT)ReferencingObject,
						*ReferencingObject->GetFullName(),
						*ClusterRootObject->GetFullName(),
						(int64)(PTRINT)Object,
						*Object->GetFullName());
					bFailed = true;
#if UE_BUILD_DEBUG
					FReferenceChainSearch RefChainSearch(Object, FReferenceChainSearch::ESearchMode::Shortest | FReferenceChainSearch::ESearchMode::PrintResults);
#endif
				}
				else if (ObjectItem->HasAnyFlags(EInternalObjectFlags::ClusterRoot))
				{
					// However, clusters need to be referenced by the current cluster otherwise they can also get GC'd too early.
					const int32 OtherClusterRootIndex = GUObjectArray.ObjectToIndex(Object);
					const FUObjectItem* OtherClusterRootItem = GUObjectArray.IndexToObjectUnsafeForGC(OtherClusterRootIndex);
					check(OtherClusterRootItem && OtherClusterRootItem->Object);
					UObject* OtherClusterRootObject = static_cast<UObject*>(OtherClusterRootItem->Object);
					UE_CLOG(OtherClusterRootIndex != ClusterRootIndex && !Cluster.ReferencedClusters.Contains(OtherClusterRootIndex), LogObj, Fatal,
						TEXT("Object %s from source cluster %s is referencing object %s (0x%016llx) from cluster %s which is not referenced by the source cluster."),
						*ReferencingObject->GetFullName(),
						*ClusterRootObject->GetFullName(),
						*Object->GetFullName(),
						(int64)(PTRINT)Object,
						*OtherClusterRootObject->GetFullName());
				}
			}
			else if (ObjectItem->GetOwnerIndex() == ClusterRootIndex)
			{
				// If this object belongs to the current cluster, keep processing its references. Otherwise ignore it as it will be processed by its cluster
				ObjectsToSerialize.Add(Object);
			}
			else
			{
				// If we're referencing an object from another cluster, make sure the other cluster is actually referenced by this cluster
				const int32 OtherClusterRootIndex = ObjectItem->GetOwnerIndex();
				check(OtherClusterRootIndex > 0);
				const FUObjectItem* OtherClusterRootItem = GUObjectArray.IndexToObjectUnsafeForGC(OtherClusterRootIndex);				
				check(OtherClusterRootItem && OtherClusterRootItem->Object);
				UObject* OtherClusterRootObject = static_cast<UObject*>(OtherClusterRootItem->Object);
				UE_CLOG(OtherClusterRootIndex != ClusterRootIndex && !Cluster.ReferencedClusters.Contains(OtherClusterRootIndex), LogObj, Fatal,
					TEXT("Object %s from source cluster %s is referencing object %s (0x%016llx) from cluster %s which is not referenced by the source cluster."),
					*ReferencingObject->GetFullName(),
					*ClusterRootObject->GetFullName(),
					*Object->GetFullName(),
					(int64)(PTRINT)Object,
					*OtherClusterRootObject->GetFullName());
			}
		}
	}
	}
};

bool VerifyClusterAssumptions(UObject* ClusterRootObject)
{
	// Collect all objects referenced by cluster root and by all objects it's referencing
	FClusterVerifyReferenceProcessor Processor(ClusterRootObject);
	TFastReferenceCollector<false, FClusterVerifyReferenceProcessor, TClusterCollector<FClusterVerifyReferenceProcessor>, FClusterArrayPool> ReferenceCollector(Processor, FClusterArrayPool::Get());
	TArray<UObject*> ObjectsToProcess;
	ObjectsToProcess.Add(ClusterRootObject);
	ReferenceCollector.CollectReferences(ObjectsToProcess);
	return Processor.NoExternalReferencesFound();
}
