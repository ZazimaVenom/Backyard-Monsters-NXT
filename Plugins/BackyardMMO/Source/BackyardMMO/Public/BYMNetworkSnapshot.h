#pragma once

#include "CoreMinimal.h"

// Bounded payload encoding for the existing Unreal RPC connection. This is
// compression and change encoding, not encryption or a separate transport.
namespace BYMNetworkSnapshot
{
    constexpr int32 MaxDecodedBytes = 4 * 1024 * 1024;
    constexpr int32 MaxDecodedCharacters = 1000000;
    constexpr int32 MaxDepth = 64;
    constexpr int32 MaxNodes = 250000;
    constexpr int32 MaxArrayEntries = 100000;

    struct BACKYARDMMO_API FEncoded
    {
        TArray<uint8> Bytes;
        int32 UncompressedBytes = 0;
        bool bCompressed = false;
        bool bDelta = false;
        bool bHasUpdate = false;
    };

    // An empty baseline or bAllowDelta=false produces a complete frame. Equal
    // JSON produces bHasUpdate=false only when deltas are allowed. Callers own
    // sequencing/acknowledgements and must retain the last acknowledged raw
    // baseline separately from any locally enriched presentation JSON.
    BACKYARDMMO_API bool Encode(const FString& CurrentJson, const FString& BaselineJson,
        bool bAllowDelta, bool bAllowCompression, FEncoded& Out, FString& Error);

    // OutJson is changed only after the complete bounded frame is validated.
    // Delta frames include a semantic baseline fingerprint to reject stale or
    // unrelated baselines. The fingerprint is not an authentication mechanism.
    BACKYARDMMO_API bool Decode(const FEncoded& Encoded, const FString& BaselineJson,
        FString& OutJson, FString& Error);
}
