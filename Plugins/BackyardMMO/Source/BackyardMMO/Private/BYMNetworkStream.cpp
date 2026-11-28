#include "BYMNetworkStream.h"

namespace BYMNetworkStream
{
namespace
{
    bool IsNewerSequence(int32 Candidate, int32 Reference)
    {
        if (Reference == 0) { return true; }
        if (Candidate == Reference) { return false; }
        int64 Distance = static_cast<int64>(Candidate) - Reference;
        if (Distance < 0) { Distance += MaxSequence; }
        return Distance > 0 && Distance <= MaxSequence / 2;
    }
}

bool FSender::Queue(const FString& Json, bool bForceFull, bool bMustDeliver, FString& Error)
{
    Error.Reset();
    if (Json.IsEmpty() || Json.Len() > BYMNetworkSnapshot::MaxDecodedCharacters)
    {
        Error = TEXT("The state update exceeds the network frame limit.");
        return false;
    }
    // Identical queued data may strengthen an existing delivery barrier without
    // consuming another slot. Never coalesce across a retained barrier.
    if (Queued.Num() > 0 && Queued.Last().Json == Json)
    {
        Queued.Last().bForceFull = Queued.Last().bForceFull || bForceFull;
        Queued.Last().bMustDeliver = Queued.Last().bMustDeliver || bMustDeliver;
        ++CoalescedUpdates;
        return true;
    }
    const bool bReplaceTail = Queued.Num() > 0 && !Queued.Last().bMustDeliver;
    const int32 RemovedCharacters = bReplaceTail ? Queued.Last().Json.Len() : 0;
    const int64 NewCharacters = static_cast<int64>(QueuedCharacters) - RemovedCharacters + Json.Len();
    if ((!bReplaceTail && Queued.Num() >= MaxQueuedFrames) || NewCharacters > MaxQueuedCharacters)
    {
        Error = TEXT("The connection cannot keep up with required state updates.");
        return false;
    }
    FQueuedFrame Frame;
    Frame.Json = Json;
    Frame.bForceFull = bForceFull;
    Frame.bMustDeliver = bMustDeliver;
    if (bReplaceTail)
    {
        // A required state supersedes the ordinary tail too, but cannot replace
        // a prior required state. This retains terminal/command transitions.
        Frame.bForceFull = Frame.bForceFull || Queued.Last().bForceFull;
        Queued.Last() = MoveTemp(Frame);
        ++CoalescedUpdates;
    }
    else { Queued.Add(MoveTemp(Frame)); }
    QueuedCharacters = static_cast<int32>(NewCharacters);
    return true;
}

bool FSender::Prepare(bool bAllowDelta, bool bAllowCompression, FString& Error)
{
    Error.Reset();
    if (HasInFlight()) { return true; }
    while (bRetryRequired || Queued.Num() > 0)
    {
        FQueuedFrame Candidate = bRetryRequired ? RetryFrame : Queued[0];
        const bool bWrapSequence = LastSequence >= MaxSequence;
        const bool bUseDelta = bAllowDelta && !Candidate.bForceFull && !bWrapSequence && !Baseline.IsEmpty();
        BYMNetworkSnapshot::FEncoded Encoded;
        if (!BYMNetworkSnapshot::Encode(Candidate.Json, bUseDelta ? Baseline : FString(),
            bUseDelta, bAllowCompression, Encoded, Error)) { return false; }
        if (bRetryRequired)
        {
            bRetryRequired = false;
            RetryFrame = FQueuedFrame();
        }
        else
        {
            QueuedCharacters -= Queued[0].Json.Len();
            Queued.RemoveAt(0);
        }
        if (!Encoded.bHasUpdate)
        {
            ++SuppressedUpdates;
            continue;
        }
        const int32 Size = Encoded.Bytes.Num();
        if (Size < 1 || Size > BYMNetworkSnapshot::MaxDecodedBytes
            || Encoded.UncompressedBytes < 1 || Encoded.UncompressedBytes > BYMNetworkSnapshot::MaxDecodedBytes)
        {
            Error = TEXT("The encoded state exceeds the network frame limit.");
            return false;
        }
        PendingFrame = MoveTemp(Candidate);
        PendingEncoded = MoveTemp(Encoded);
        LastSequence = bWrapSequence ? 1 : LastSequence + 1;
        PendingSequence = LastSequence;
        PendingBaseSequence = PendingEncoded.bDelta ? BaselineSequence : 0;
        PendingChunkCount = (Size + ChunkBytes - 1) / ChunkBytes;
        NextChunk = 0;
        LastAcknowledgedChunk = -1;
        return true;
    }
    return true;
}

bool FSender::TakeNextChunk(FChunk& Out)
{
    if (!HasChunksToSend()) { return false; }
    const int32 Offset = NextChunk * ChunkBytes;
    const int32 Length = FMath::Min(ChunkBytes, PendingEncoded.Bytes.Num() - Offset);
    Out = FChunk();
    Out.Sequence = PendingSequence;
    Out.BaseSequence = PendingBaseSequence;
    Out.Index = NextChunk;
    Out.Count = PendingChunkCount;
    Out.RawBytes = PendingEncoded.UncompressedBytes;
    Out.bCompressed = PendingEncoded.bCompressed;
    Out.bDelta = PendingEncoded.bDelta;
    Out.Bytes.Append(PendingEncoded.Bytes.GetData() + Offset, Length);
    ++NextChunk;
    PayloadBytesSent += static_cast<uint64>(Length);
    if (NextChunk == PendingChunkCount) { ++FramesSent; }
    return true;
}

bool FSender::AcknowledgeChunk(int32 Sequence, int32 Index)
{
    if (!HasInFlight() || Sequence != PendingSequence || NextChunk == 0
        || Index != NextChunk - 1 || Index <= LastAcknowledgedChunk
        || Index < 0 || Index + 1 >= PendingChunkCount) { return false; }
    LastAcknowledgedChunk = Index;
    return true;
}

void FSender::ClearPending()
{
    PendingFrame = FQueuedFrame();
    PendingEncoded = BYMNetworkSnapshot::FEncoded();
    PendingSequence = 0;
    PendingBaseSequence = 0;
    PendingChunkCount = 0;
    NextChunk = 0;
    LastAcknowledgedChunk = -1;
}

bool FSender::Acknowledge(int32 Sequence, bool bAccepted)
{
    if (!HasInFlight() || Sequence != PendingSequence || NextChunk == 0
        || (bAccepted && NextChunk != PendingChunkCount)) { return false; }
    if (bAccepted)
    {
        Baseline = MoveTemp(PendingFrame.Json);
        BaselineSequence = PendingSequence;
        ConsecutiveRejects = 0;
    }
    else
    {
        // An early negative ACK cancels unsent chunks, preventing a rejected
        // first chunk from leaving the sender waiting forever for a final ACK.
        Baseline.Reset();
        BaselineSequence = 0;
        RetryFrame = MoveTemp(PendingFrame);
        RetryFrame.bForceFull = true;
        RetryFrame.bMustDeliver = true;
        bRetryRequired = true;
        ++ConsecutiveRejects;
    }
    ClearPending();
    return true;
}

void FSender::Reset()
{
    *this = FSender();
}

void FReceiver::ClearIncoming()
{
    Incoming = FChunk();
    Bytes.Reset();
    NextChunk = 0;
}

EReceiveResult FReceiver::Reject(int32 Sequence, const TCHAR* Message, FString& Error)
{
    Error = Message;
    // Keep a sequence watermark even when the malformed sequence itself is
    // invalid. Clearing a baseline must not make an older full frame new again.
    for (const int32 Seen : {BaselineSequence, Incoming.Sequence, Sequence})
        if (Seen > 0 && Seen <= MaxSequence && IsNewerSequence(Seen, LastRejectedSequence))
            LastRejectedSequence = Seen;
    ClearIncoming();
    Baseline.Reset();
    BaselineSequence = 0;
    return EReceiveResult::Rejected;
}

EReceiveResult FReceiver::Accept(const FChunk& Chunk, FString& OutJson, FString& Error)
{
    Error.Reset();
    if (Chunk.Sequence < 1 || Chunk.Sequence > MaxSequence)
        return Reject(Chunk.Sequence, TEXT("Invalid state frame sequence."), Error);
    if (Incoming.Sequence != 0 && Chunk.Sequence != Incoming.Sequence
        && !IsNewerSequence(Chunk.Sequence, Incoming.Sequence)) { return EReceiveResult::Stale; }
    if (Incoming.Sequence == 0
        && (!IsNewerSequence(Chunk.Sequence, BaselineSequence)
            || !IsNewerSequence(Chunk.Sequence, LastRejectedSequence))) { return EReceiveResult::Stale; }
    if (Chunk.Count < 1 || Chunk.Count > MaxChunks || Chunk.Index < 0 || Chunk.Index >= Chunk.Count
        || Chunk.Bytes.Num() < 1 || Chunk.Bytes.Num() > ChunkBytes
        || (Chunk.Index + 1 < Chunk.Count && Chunk.Bytes.Num() != ChunkBytes)
        || Chunk.RawBytes < 1 || Chunk.RawBytes > BYMNetworkSnapshot::MaxDecodedBytes
        || Chunk.BaseSequence < 0 || Chunk.BaseSequence > MaxSequence
        || (!Chunk.bDelta && Chunk.BaseSequence != 0)
        || (Chunk.bDelta && Chunk.BaseSequence == 0))
        return Reject(Chunk.Sequence, TEXT("Invalid state frame bounds."), Error);
    const int64 FrameBytesAtChunk = static_cast<int64>(Chunk.Index) * ChunkBytes + Chunk.Bytes.Num();
    if (FrameBytesAtChunk > BYMNetworkSnapshot::MaxDecodedBytes
        || (!Chunk.bCompressed && (Chunk.Count != (Chunk.RawBytes + ChunkBytes - 1) / ChunkBytes
            || Chunk.Bytes.Num() != FMath::Min(ChunkBytes, Chunk.RawBytes - Chunk.Index * ChunkBytes))))
        return Reject(Chunk.Sequence, TEXT("Invalid state chunk length."), Error);
    if (Chunk.Index == 0)
    {
        if (Incoming.Sequence != 0)
            return Reject(Chunk.Sequence, TEXT("Overlapping state frames."), Error);
        if (Chunk.bDelta && (Baseline.IsEmpty() || Chunk.BaseSequence != BaselineSequence))
            return Reject(Chunk.Sequence, TEXT("The state baseline needs a full refresh."), Error);
        Incoming = Chunk;
        Incoming.Bytes.Reset();
        Bytes.Reset();
        Bytes.Reserve(FMath::Min(Chunk.Count * ChunkBytes, BYMNetworkSnapshot::MaxDecodedBytes));
        NextChunk = 0;
    }
    if (Incoming.Sequence != Chunk.Sequence || NextChunk != Chunk.Index
        || Incoming.BaseSequence != Chunk.BaseSequence || Incoming.Count != Chunk.Count
        || Incoming.RawBytes != Chunk.RawBytes || Incoming.bCompressed != Chunk.bCompressed
        || Incoming.bDelta != Chunk.bDelta)
        return Reject(Chunk.Sequence, TEXT("Out-of-order or mixed state chunks."), Error);
    Bytes.Append(Chunk.Bytes.GetData(), Chunk.Bytes.Num());
    ++NextChunk;
    if (NextChunk < Incoming.Count) { return EReceiveResult::Pending; }
    BYMNetworkSnapshot::FEncoded Encoded;
    Encoded.Bytes = MoveTemp(Bytes);
    Encoded.UncompressedBytes = Incoming.RawBytes;
    Encoded.bCompressed = Incoming.bCompressed;
    Encoded.bDelta = Incoming.bDelta;
    Encoded.bHasUpdate = true;
    FString Decoded;
    if (!BYMNetworkSnapshot::Decode(Encoded, Baseline, Decoded, Error))
    {
        const FString DecodeError = Error;
        return Reject(Chunk.Sequence, *DecodeError, Error);
    }
    Baseline = Decoded;
    BaselineSequence = Chunk.Sequence;
    // A completed full frame establishes a new baseline after recovery/wrap.
    LastRejectedSequence = 0;
    OutJson = MoveTemp(Decoded);
    ClearIncoming();
    return EReceiveResult::Complete;
}

void FReceiver::Reset()
{
    *this = FReceiver();
}
}
