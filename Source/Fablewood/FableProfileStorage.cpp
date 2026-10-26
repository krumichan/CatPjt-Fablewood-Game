#include "FableProfileStorage.h"
#include "FableProfileSchema.h"
#include "FableSaveGame.h"
#include "Kismet/GameplayStatics.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

namespace
{
    /** backend 식별자나 경로를 파일 슬롯으로 사용하지 않는다. */
    bool SafeSlot(const FString& Slot)
    {
        if (Slot.IsEmpty() || Slot.Len() > 128)
        {
            return false;
        }
        for (TCHAR C : Slot)
        {
            if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_' || C == '-'))
            {
                return false;
            }
        }
        return true;
    }
}

/** Base64 문자열이 아닌 원본 바이너리를 해시한다. */
FString FableProfileStorage::HashBytes(const TArray<uint8>& Bytes)
{
    uint8 Digest[SHA256_DIGEST_LENGTH];
    return SHA256(Bytes.GetData(), static_cast<size_t>(Bytes.Num()), Digest)
               ? BytesToHex(Digest, SHA256_DIGEST_LENGTH).ToLower()
               : FString();
}

/** 메뉴 미리보기도 이 함수만 읽으며 backup이나 index를 쓰지 않는다. */
UFableSaveGame* FableProfileStorage::ReadCandidate(const FString& Slot, TArray<uint8>& OriginalBytes)
{
    OriginalBytes.Reset();
    if (!SafeSlot(Slot) || !UGameplayStatics::LoadDataFromSlot(OriginalBytes, Slot, 0) || OriginalBytes.IsEmpty() ||
        OriginalBytes.Num() > 262144)
    {
        return nullptr;
    }

    const auto* Original = Cast<UFableSaveGame>(UGameplayStatics::LoadGameFromMemory(OriginalBytes));
    return FableProfileSchema::MakeCandidate(Original);
}

/** 原本を移行せず、実際に書かれたcurrent bytesだけをindexの前提にする。 */
UFableSaveGame* FableProfileStorage::ReadVerifiedCurrent(const FString& Slot)
{
    TArray<uint8> Bytes;
    if (!SafeSlot(Slot) || !UGameplayStatics::LoadDataFromSlot(Bytes, Slot, 0) || Bytes.IsEmpty() ||
        Bytes.Num() > 262144)
    {
        return nullptr;
    }

    auto* Current = Cast<UFableSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
    return Current && FableProfileSchema::IsWritableCurrentVersion(Current->Version) && Current->IsValidProfile()
               ? Current
               : nullptr;
}

/** backup readback 실패 시 원본을 기록하지 않는다. 실제 쓰기 실패 후에도 backup은 남는다. */
bool FableProfileStorage::WriteVerified(const FString& Slot, const UFableSaveGame* Candidate)
{
    if (!SafeSlot(Slot) || !Candidate || !FableProfileSchema::IsWritableCurrentVersion(Candidate->Version) ||
        !Candidate->IsValidProfile())
    {
        return false;
    }

    if (UGameplayStatics::DoesSaveGameExist(Slot, 0))
    {
        TArray<uint8> OriginalBytes;
        UFableSaveGame* Validated = ReadCandidate(Slot, OriginalBytes);
        const auto* Original = Cast<UFableSaveGame>(UGameplayStatics::LoadGameFromMemory(OriginalBytes));
        if (!Validated || !Original)
        {
            return false;
        }

        if (Original->Version < FableProfileSchema::CurrentVersion)
        {
            const FString OriginalHash = HashBytes(OriginalBytes);
            const FString Backup = TEXT("Fablewood_V10Backup_") + OriginalHash;
            if (OriginalHash.Len() != 64 || !SafeSlot(Backup))
            {
                return false;
            }
            if (!UGameplayStatics::DoesSaveGameExist(Backup, 0) &&
                !UGameplayStatics::SaveDataToSlot(OriginalBytes, Backup, 0))
            {
                return false;
            }

            TArray<uint8> Readback;
            if (!UGameplayStatics::LoadDataFromSlot(Readback, Backup, 0) || Readback != OriginalBytes ||
                HashBytes(Readback) != OriginalHash)
            {
                return false;
            }
        }
    }

    TArray<uint8> Bytes, Readback;
    if (!UGameplayStatics::SaveGameToMemory(const_cast<UFableSaveGame*>(Candidate), Bytes) || Bytes.IsEmpty() ||
        Bytes.Num() > 262144 || !UGameplayStatics::SaveDataToSlot(Bytes, Slot, 0) ||
        !UGameplayStatics::LoadDataFromSlot(Readback, Slot, 0) || Bytes != Readback || HashBytes(Bytes).IsEmpty())
    {
        return false;
    }

    const auto* Written = Cast<UFableSaveGame>(UGameplayStatics::LoadGameFromMemory(Readback));
    return Written && Written->Version == FableProfileSchema::CurrentVersion && Written->IsValidProfile();
}
