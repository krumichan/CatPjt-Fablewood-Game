#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FableTownTravelTypes.h"
#include "FableTownStreamingSubsystem.generated.h"

class ULevelStreamingDynamic;

/** 월드별 공유 지형과 정확한 lease만 소유한다. 이동/RPC 권한은 없다. / 共有地形とleaseのみを所有する。 */
UCLASS()
class FABLEWOOD_API UFableTownStreamingSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void OnWorldEndPlay(UWorld& InWorld) override;

    bool AcquireGeometry(const FFableTownGeometryDescriptor& Descriptor,
                         const FFableTownGeometryLeaseOwner& TrustedOwner, FFableTownGeometryLease& OutLease,
                         EFableTownGeometryFailure& OutFailure);
    FFableTownGeometryStatus GetGeometryStatus(const FFableTownGeometryLease& Lease) const;
    bool IsGeometryReadyVisible(const FFableTownGeometryLease& Lease) const;
    bool ReleaseGeometry(const FFableTownGeometryLease& ExactOwnedLease);
    FOnFableTownGeometryChanged& OnGeometryChanged();

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    struct FGeometryEntry
    {
        FFableTownGeometryDescriptor Descriptor;
        FFableTownGeometryStatus Status;
        TSet<FGuid> Tokens;
        bool bRemoving = false;
    };

    struct FLeaseRecord
    {
        FFableTownGeometryLease Lease;
        FFableTownGeometryLeaseOwner Owner;
        int64 EntryKey = 0;
    };

    static int64 MakeEntryKey(EFableTownId TownId, int32 CatalogVersion);
    static bool SameDescriptor(const FFableTownGeometryDescriptor& Left, const FFableTownGeometryDescriptor& Right);
    static bool SameOwner(const FFableTownGeometryLeaseOwner& Left, const FFableTownGeometryLeaseOwner& Right);
    static bool SameLease(const FFableTownGeometryLease& Left, const FFableTownGeometryLease& Right);
    bool ValidateDescriptor(const FFableTownGeometryDescriptor& Descriptor,
                            EFableTownGeometryFailure& OutFailure) const;
    bool ValidateOwner(const FFableTownGeometryLeaseOwner& Owner) const;
    bool IsExpectedPersistentWorld() const;
    FFableTownGeometryStatus ObserveEntry(int64 EntryKey, const FGeometryEntry& Entry) const;
    void EnsureMonitor();
    void PollGeometry();
    void BeginRemoval(int64 EntryKey, FGeometryEntry& Entry);
    void StopAndFence();

    UPROPERTY(Transient)
    TMap<int64, TObjectPtr<ULevelStreamingDynamic>> RetainedStreams;

    TMap<int64, FGeometryEntry> Entries;
    TMap<FGuid, FLeaseRecord> Leases;
    FOnFableTownGeometryChanged GeometryChanged;
    FTimerHandle MonitorTimer;
    int64 LastEntryEpoch = 0;
    bool bStopping = false;

#if WITH_DEV_AUTOMATION_TESTS
    friend struct FFableTownStreamingTestAccess;
#endif
};
