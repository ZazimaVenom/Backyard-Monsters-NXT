#pragma once

#include "CoreMinimal.h"
#include "BYMNetworkSnapshot.h"

// Bounded framing for an ordered reliable Unreal RPC stream. Each controller
// owns a separate sender/receiver per payload channel; no state is shared across
// accounts or mixed with locally enriched presentation JSON.
namespace BYMNetworkStream
{
    constexpr int32 ChunkBytes = 8192;
    constexpr int32 MaxChunks = BYMNetworkSnapshot::MaxDecodedBytes / ChunkBytes;
    constexpr int32 MaxQueuedFrames = 8;
    constexpr int32 MaxQueuedCharacters = 2 * BYMNetworkSnapshot::MaxDecodedCharacters;
    constexpr int32 MaxSequence = 0x3fffffff;

    struct BACKYARDMMO_API FChunk
    {
        int32 Sequence = 0;
        int32 BaseSequence = 0;
        int32 Index = 0;
        int32 Count = 0;
        int32 RawBytes = 0;
        bool bCompressed = false;
        bool bDelta = false;
        TArray<uint8> Bytes;
    };

    class BACKYARDMMO_API FSender
    {
    public:
        // Ordinary queued updates replace only the ordinary tail. Must-deliver
        // barriers retain command/view/terminal state in order. Overflow fails
        // without changing the existing queue; callers must handle that error.
        bool Queue(const FString& Json, bool bForceFull, bool bMustDeliver, FString& Error);
        // True means no error, including no work or waiting for an ACK. At most
        // one frame may be in flight. Call TakeNextChunk under a per-tick budget.
        bool Prepare(bool bAllowDelta, bool bAllowCompression, FString& Error);
        // One chunk credit per stream prevents a large frame from filling the
        // native reliable RPC queue. Pending receives ACK each nonfinal chunk;
        // the complete-frame ACK commits the final chunk and its baseline.
        bool TakeNextChunk(FChunk& Out);
        bool AcknowledgeChunk(int32 Sequence, int32 Index);
        // Only an exact pending sequence after all chunks were handed off may
        // positively ACK. An exact negative ACK after any sent chunk cancels the
        // rest and retries as a full frame on a later Prepare.
        bool Acknowledge(int32 Sequence, bool bAccepted);
        void Reset();

        bool HasInFlight() const { return PendingSequence != 0; }
        bool HasChunksToSend() const
        { return HasInFlight() && NextChunk < PendingChunkCount && NextChunk == LastAcknowledgedChunk + 1; }
        int32 GetInFlightSequence() const { return PendingSequence; }
        int32 GetQueuedFrames() const { return Queued.Num() + (bRetryRequired ? 1 : 0); }
        int32 GetPendingChunks() const { return PendingChunkCount - NextChunk; }
        int32 GetConsecutiveRejects() const { return ConsecutiveRejects; }
        uint64 GetPayloadBytesSent() const { return PayloadBytesSent; }
        uint64 GetFramesSent() const { return FramesSent; }
        uint64 GetCoalescedUpdates() const { return CoalescedUpdates; }
        uint64 GetSuppressedUpdates() const { return SuppressedUpdates; }

    private:
        struct FQueuedFrame
        {
            FString Json;
            bool bForceFull = false;
            bool bMustDeliver = false;
        };
        TArray<FQueuedFrame> Queued;
        int32 QueuedCharacters = 0;
        FQueuedFrame RetryFrame;
        bool bRetryRequired = false;
        FString Baseline;
        int32 BaselineSequence = 0;
        int32 LastSequence = 0;
        FQueuedFrame PendingFrame;
        BYMNetworkSnapshot::FEncoded PendingEncoded;
        int32 PendingSequence = 0;
        int32 PendingBaseSequence = 0;
        int32 PendingChunkCount = 0;
        int32 NextChunk = 0;
        int32 LastAcknowledgedChunk = -1;
        int32 ConsecutiveRejects = 0;
        uint64 PayloadBytesSent = 0;
        uint64 FramesSent = 0;
        uint64 CoalescedUpdates = 0;
        uint64 SuppressedUpdates = 0;
        void ClearPending();
    };

    enum class EReceiveResult : uint8 { Pending, Complete, Rejected, Stale };

    class BACKYARDMMO_API FReceiver
    {
    public:
        // OutJson changes only on Complete. ACK Complete(true)/Rejected(false)
        // with the frame sequence; Pending grants a chunk credit using sequence
        // and index. Stale chunks require no ACK.
        EReceiveResult Accept(const FChunk& Chunk, FString& OutJson, FString& Error);
        void Reset();
        int32 GetBaselineSequence() const { return BaselineSequence; }
        const FString& GetBaselineJson() const { return Baseline; }
    private:
        FString Baseline;
        int32 BaselineSequence = 0;
        FChunk Incoming;
        TArray<uint8> Bytes;
        int32 NextChunk = 0;
        int32 LastRejectedSequence = 0;
        void ClearIncoming();
        EReceiveResult Reject(int32 Sequence, const TCHAR* Message, FString& Error);
    };
}
