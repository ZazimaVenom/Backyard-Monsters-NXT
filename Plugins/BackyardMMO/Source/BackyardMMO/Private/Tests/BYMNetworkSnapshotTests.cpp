#include "BYMNetworkSnapshot.h"
#include "BYMNetworkStream.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace BYMNetworkSnapshotTests
{
    using FObject = TSharedPtr<FJsonObject>;
    using FArray = TArray<TSharedPtr<FJsonValue>>;
    FObject Parse(const FString& Text)
    { FObject Result; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Result); return Result; }
    FString Write(const FObject& Object)
    { FString Result; FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&Result)); return Result; }
    bool EqualValue(const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
    {
        if (!A.IsValid() || !B.IsValid()) return A.IsValid() == B.IsValid();
        if (A->Type != B->Type) return false;
        switch (A->Type)
        {
        case EJson::Null: return true;
        case EJson::String: return A->AsString() == B->AsString();
        case EJson::Number: return A->AsNumber() == B->AsNumber();
        case EJson::Boolean: return A->AsBool() == B->AsBool();
        case EJson::Array:
        {
            const FArray& Left = A->AsArray(); const FArray& Right = B->AsArray();
            if (Left.Num() != Right.Num()) return false;
            for (int32 Index = 0; Index < Left.Num(); ++Index) if (!EqualValue(Left[Index], Right[Index])) return false;
            return true;
        }
        case EJson::Object:
        {
            const FObject Left = A->AsObject(), Right = B->AsObject();
            if (!Left.IsValid() || !Right.IsValid() || Left->Values.Num() != Right->Values.Num()) return false;
            for (const auto& Pair : Left->Values)
            {
                const FString Key(*Pair.Key);
                const TSharedPtr<FJsonValue> Other = Right->TryGetField(Key);
                if (!Other.IsValid() || !EqualValue(Pair.Value, Other)) return false;
            }
            return true;
        }
        default: return false;
        }
    }
    bool EqualJson(const FString& A, const FString& B)
    {
        const FObject Left = Parse(A), Right = Parse(B);
        return Left.IsValid() && Right.IsValid()
            && EqualValue(MakeShared<FJsonValueObject>(Left), MakeShared<FJsonValueObject>(Right));
    }
    FObject Rich()
    {
        FObject Result = MakeShared<FJsonObject>(); Result->SetNumberField(TEXT("now"), 1000.);
        Result->SetStringField(TEXT("selfId"), TEXT("network-owner"));
        FArray Buildings;
        for (int32 Index = 0; Index < 120; ++Index)
        {
            FObject B = MakeShared<FJsonObject>(); B->SetStringField(TEXT("id"), FString::Printf(TEXT("building-%d"), Index));
            B->SetNumberField(TEXT("hp"), 1000.); B->SetNumberField(TEXT("x"), Index); B->SetNumberField(TEXT("y"), Index % 10);
            B->SetStringField(TEXT("upgradeReason"), TEXT("Build and upgrade your Town Hall to improve this original backyard building."));
            Buildings.Add(MakeShared<FJsonValueObject>(B));
        }
        Result->SetArrayField(TEXT("buildings"), Buildings);
        Result->SetObjectField(TEXT("empty"), MakeShared<FJsonObject>());
        return Result;
    }
    BYMNetworkSnapshot::FEncoded Raw(const FString& Text)
    {
        BYMNetworkSnapshot::FEncoded Encoded; const FTCHARToUTF8 UTF8(*Text);
        Encoded.Bytes.Append(reinterpret_cast<const uint8*>(UTF8.Get()), UTF8.Length());
        Encoded.UncompressedBytes = UTF8.Length(); Encoded.bHasUpdate = true; return Encoded;
    }
    bool RoundTrip(FAutomationTestBase& Test, const FString& Current, const FString& Baseline, bool Delta, bool Compression,
        BYMNetworkSnapshot::FEncoded& Packet)
    {
        FString Error, Received = TEXT("unchanged output");
        if (!Test.TestTrue(TEXT("Actual codec encodes the valid snapshot"), BYMNetworkSnapshot::Encode(Current, Baseline, Delta, Compression, Packet, Error)))
        { Test.AddError(Error); return false; }
        if (!Test.TestTrue(TEXT("The changed snapshot supplies a frame"), Packet.bHasUpdate)) return false;
        if (!Test.TestTrue(TEXT("Actual codec decodes its frame"), BYMNetworkSnapshot::Decode(Packet, Baseline, Received, Error)))
        { Test.AddError(Error); return false; }
        return Test.TestTrue(TEXT("Every reconstructed JSON value equals the original snapshot"), EqualJson(Current, Received));
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMNetworkDeltaTest, "BackyardMMO.Network.FullDeltaAndUnchangedSnapshots",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMNetworkDeltaTest::RunTest(const FString& Parameters)
    {
        using namespace BYMNetworkSnapshot;
        FObject Before = Rich(); const FString Baseline = Write(Before);
        FEncoded Full; if (!RoundTrip(*this, Baseline, FString(), true, true, Full)) return false;
        TestFalse(TEXT("Initial frame must be self-contained"), Full.bDelta);
        FObject After = Parse(Baseline); After->SetNumberField(TEXT("now"), 1002.);
        const FArray* Buildings = nullptr; After->TryGetArrayField(TEXT("buildings"), Buildings);
        if (!TestTrue(TEXT("The fixture has a real array"), Buildings && Buildings->Num() == 120)) return false;
        (*Buildings)[35]->AsObject()->SetNumberField(TEXT("hp"), 987.);
        (*Buildings)[35]->AsObject()->RemoveField(TEXT("upgradeReason"));
        After->RemoveField(TEXT("empty")); After->SetArrayField(TEXT("newArray"), FArray());
        FEncoded Delta; const FString Current = Write(After);
        if (!RoundTrip(*this, Current, Baseline, true, true, Delta)) return false;
        TestTrue(TEXT("A sparse building change uses a delta"), Delta.bDelta);
        TestTrue(TEXT("A sparse delta is smaller than the full compressed yard"), Delta.Bytes.Num() < Full.Bytes.Num());
        FEncoded Unchanged; FString Error;
        TestTrue(TEXT("The same semantic snapshot is accepted"), Encode(Current, Current, true, true, Unchanged, Error));
        TestFalse(TEXT("No data is emitted for an unchanged snapshot"), Unchanged.bHasUpdate);
        TestTrue(TEXT("No-change payload is empty"), Unchanged.Bytes.IsEmpty());
        FObject Reordered = MakeShared<FJsonObject>();
        TArray<FString> Keys; for (const auto& Pair : Before->Values) Keys.Add(FString(*Pair.Key));
        for (int32 Index = Keys.Num() - 1; Index >= 0; --Index)
            for (const auto& Pair : Before->Values) if (FString(*Pair.Key) == Keys[Index]) Reordered->SetField(Keys[Index], Pair.Value);
        TestTrue(TEXT("Key-order-only changes are accepted"), Encode(Write(Reordered), Baseline, true, true, Unchanged, Error));
        TestFalse(TEXT("Object key order cannot trigger redundant sends"), Unchanged.bHasUpdate);
        FString Received;
        TestTrue(TEXT("Equivalent reordered baseline decodes a delta"), Decode(Delta, Write(Reordered), Received, Error));
        TestTrue(TEXT("Reordered baseline preserves all values"), EqualJson(Current, Received));
        FEncoded Forced; if (!RoundTrip(*this, Current, Current, false, true, Forced)) return false;
        TestFalse(TEXT("Explicit resynchronization forces a complete frame"), Forced.bDelta);
        Before->SetNumberField(TEXT("now"), 999.); Received = TEXT("sentinel");
        TestFalse(TEXT("A delta refuses an unrelated baseline"), Decode(Delta, Write(Before), Received, Error));
        TestEqual(TEXT("Wrong baseline leaves client state untouched"), Received, FString(TEXT("sentinel")));
        TestFalse(TEXT("A delta refuses a missing baseline"), Decode(Delta, FString(), Received, Error));
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMNetworkShapeTest, "BackyardMMO.Network.ArrayShapeTypesAndUnicode",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMNetworkShapeTest::RunTest(const FString& Parameters)
    {
        using namespace BYMNetworkSnapshot;
        const FString Baseline = Write(Rich()); FObject Value = Parse(Baseline);
        Value->SetStringField(TEXT("name"), TEXT("Pokey \u00e9 \u4e16\u754c \U0001f47e"));
        Value->SetField(TEXT("nullable"), MakeShared<FJsonValueNull>());
        Value->SetBoolField(TEXT("ready"), true);
        FEncoded Packet; if (!RoundTrip(*this, Write(Value), Baseline, true, true, Packet)) return false;
        FString Previous = Write(Value);
        FArray Changed; Changed.Add(MakeShared<FJsonValueString>(TEXT("different type"))); Changed.Add(MakeShared<FJsonValueNull>());
        Value->SetArrayField(TEXT("buildings"), Changed); Value->SetObjectField(TEXT("name"), MakeShared<FJsonObject>());
        if (!RoundTrip(*this, Write(Value), Previous, true, true, Packet)) return false;
        Previous = Write(Value); Value->SetArrayField(TEXT("buildings"), FArray()); Value->SetBoolField(TEXT("ready"), false);
        if (!RoundTrip(*this, Write(Value), Previous, true, false, Packet)) return false;
        TestFalse(TEXT("Disabling compression leaves an ordinary byte payload"), Packet.bCompressed);
        Previous = Write(Value); Value->SetStringField(TEXT("buildings"), TEXT("type replacement")); Value->RemoveField(TEXT("nullable"));
        return RoundTrip(*this, Write(Value), Previous, true, true, Packet);
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMNetworkBoundsTest, "BackyardMMO.Network.RejectMalformedAndOversizedFrames",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMNetworkBoundsTest::RunTest(const FString& Parameters)
    {
        using namespace BYMNetworkSnapshot;
        FEncoded Packet; const FString Full = Write(Rich());
        if (!RoundTrip(*this, Full, FString(), false, true, Packet)) return false;
        if (!TestTrue(TEXT("Repetitive full snapshots exercise real compression"), Packet.bCompressed)) return false;
        const auto Reject = [this](FEncoded Candidate, const TCHAR* Label)
        {
            FString Output = TEXT("do not replace"), Error;
            TestFalse(Label, Decode(Candidate, FString(), Output, Error));
            TestEqual(TEXT("Rejected input leaves old client snapshot unchanged"), Output, FString(TEXT("do not replace")));
        };
        FEncoded Bad = Packet; Bad.Bytes[0] ^= 0xff; Reject(Bad, TEXT("Corrupt compressed header is rejected"));
        Bad = Packet; Bad.Bytes.RemoveAt(Bad.Bytes.Num() - 1); Reject(Bad, TEXT("Truncated compressed stream is rejected"));
        Bad = Packet; Bad.UncompressedBytes = MaxDecodedBytes + 1; Reject(Bad, TEXT("Excessive decompression allocation is rejected"));
        Bad = Packet; Bad.UncompressedBytes = -1; Reject(Bad, TEXT("Negative output size is rejected"));
        Bad = Packet; Bad.UncompressedBytes += 1; Reject(Bad, TEXT("Inexact decompressed size is rejected"));
        Bad = Raw(TEXT("{broken json")); Reject(Bad, TEXT("Malformed JSON is rejected"));
        Bad = Raw(TEXT("[]")); Reject(Bad, TEXT("Non-object snapshot is rejected"));
        Bad = Raw(TEXT("{\"bad\\u0000key\":1}")); Reject(Bad, TEXT("Embedded-NUL object keys are rejected"));
        Bad = Raw(TEXT("{\"x\":1}")); Bad.Bytes[0] = 0xff; Reject(Bad, TEXT("Invalid UTF-8 is rejected"));
        Bad = Raw(TEXT("{}")); Bad.UncompressedBytes += 1; Reject(Bad, TEXT("Raw size mismatch is rejected"));
        FString Deep; for (int32 Index = 0; Index < MaxDepth + 5; ++Index) Deep += TEXT("{\"x\":");
        Deep += TEXT("0"); for (int32 Index = 0; Index < MaxDepth + 5; ++Index) Deep += TEXT("}");
        Reject(Raw(Deep), TEXT("Pathological JSON depth is rejected before it can exhaust recursion"));
        FString Error;
        TestFalse(TEXT("Oversized source JSON cannot be encoded"), Encode(FString::ChrN(MaxDecodedCharacters + 1, TEXT('x')), FString(), false, true, Bad, Error));
        TestFalse(TEXT("Malformed source JSON cannot be encoded"), Encode(TEXT("{"), FString(), false, true, Bad, Error));
        return true;
    }


    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMNetworkInvalidDeltaTest, "BackyardMMO.Network.RejectInvalidDeltaOperationsAtomically",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMNetworkInvalidDeltaTest::RunTest(const FString& Parameters)
    {
        using namespace BYMNetworkSnapshot;
        const FString Before = Write(Rich()); FObject After = Parse(Before); After->SetNumberField(TEXT("now"), 1001.);
        FEncoded Delta; if (!RoundTrip(*this, Write(After), Before, true, false, Delta)) return false;
        if (!TestTrue(TEXT("Malformed-patch fixture starts with a genuine raw delta"), Delta.bDelta && !Delta.bCompressed)) return false;
        const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Delta.Bytes.GetData()), Delta.Bytes.Num());
        const FObject Envelope = Parse(FString(Text.Length(), Text.Get()));
        const auto RejectPatch = [this, &Before, &Envelope](const TCHAR* PatchJson, const TCHAR* Label)
        {
            const FObject Candidate = Parse(Write(Envelope)); Candidate->SetObjectField(TEXT("patch"), Parse(PatchJson));
            FEncoded Bad = Raw(Write(Candidate)); Bad.bDelta = true;
            FString Output = TEXT("stable baseline"), Error;
            TestFalse(Label, Decode(Bad, Before, Output, Error));
            TestEqual(TEXT("A malformed patch cannot partially commit state"), Output, FString(TEXT("stable baseline")));
        };
        RejectPatch(TEXT("{\"o\":{},\"r\":[\"missing\"]}"), TEXT("Deleting an absent key is rejected"));
        RejectPatch(TEXT("{\"o\":{},\"r\":[\"empty\",\"empty\"]}"), TEXT("Duplicate key removals are rejected"));
        RejectPatch(TEXT("{\"o\":{\"empty\":{\"v\":1}},\"r\":[\"empty\"]}"), TEXT("Changing and removing the same key is rejected"));
        RejectPatch(TEXT("{\"o\":{\"buildings\":{\"a\":{},\"n\":99999}},\"r\":[]}"), TEXT("Sparse huge array expansion is rejected"));
        RejectPatch(TEXT("{\"o\":{\"buildings\":{\"a\":{\"00\":{\"v\":1}},\"n\":120}},\"r\":[]}"), TEXT("Ambiguous leading-zero array indices are rejected"));
        RejectPatch(TEXT("{\"o\":{\"buildings\":{\"a\":{},\"n\":1.5}},\"r\":[]}"), TEXT("Fractional array length is rejected"));
        RejectPatch(TEXT("{\"v\":[]}"), TEXT("A patch cannot replace the snapshot root with an array"));
        RejectPatch(TEXT("{\"v\":{},\"extra\":true}"), TEXT("Conflicting patch modes and unknown fields are rejected"));
        return true;
    }

    bool Deliver(FAutomationTestBase& Test, BYMNetworkStream::FSender& Sender, BYMNetworkStream::FReceiver& Receiver,
        const FString& Expected, bool Compression = true)
    {
        using namespace BYMNetworkStream;
        FString Error, Output = TEXT("not complete");
        if (!Test.TestTrue(TEXT("Sender prepares a bounded frame"), Sender.Prepare(true, Compression, Error))) return false;
        FChunk Chunk; int32 Count = 0; EReceiveResult Last = EReceiveResult::Pending;
        while (Sender.TakeNextChunk(Chunk))
        {
            ++Count;
            if (!Test.TestTrue(TEXT("Every RPC payload respects the fixed chunk budget"), Chunk.Bytes.Num() > 0 && Chunk.Bytes.Num() <= ChunkBytes)) return false;
            Last = Receiver.Accept(Chunk, Output, Error);
            if (!Test.TestTrue(TEXT("Ordered chunks remain pending until complete"), Last == EReceiveResult::Pending || Last == EReceiveResult::Complete)) return false;
            if (Last == EReceiveResult::Pending)
            {
                Test.TestEqual(TEXT("Partial frames cannot replace presentation state"), Output, FString(TEXT("not complete")));
                FChunk Blocked;
                Test.TestFalse(TEXT("A stalled client cannot accumulate more reliable chunks"), Sender.TakeNextChunk(Blocked));
                Test.TestFalse(TEXT("Partial transfer cannot commit a complete baseline"), Sender.Acknowledge(Chunk.Sequence, true));
                Test.TestFalse(TEXT("An unrelated chunk ACK cannot advance sending"), Sender.AcknowledgeChunk(Chunk.Sequence + 1, Chunk.Index));
                Test.TestFalse(TEXT("A future chunk cannot be acknowledged"), Sender.AcknowledgeChunk(Chunk.Sequence, Chunk.Index + 1));
                Test.TestTrue(TEXT("Exact received chunk grants the next bounded credit"), Sender.AcknowledgeChunk(Chunk.Sequence, Chunk.Index));
                Test.TestFalse(TEXT("Duplicate chunk acknowledgement is ignored"), Sender.AcknowledgeChunk(Chunk.Sequence, Chunk.Index));
            }
        }
        if (!Test.TestTrue(TEXT("A changed frame has at least one chunk"), Count > 0)) return false;
        if (!Test.TestTrue(TEXT("The final chunk commits a complete snapshot"), Last == EReceiveResult::Complete)) return false;
        Test.TestTrue(TEXT("The reconstructed stream frame equals the source"), EqualJson(Expected, Output));
        return Test.TestTrue(TEXT("Exact completed sequence may be acknowledged"), Sender.Acknowledge(Chunk.Sequence, true));
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMNetworkBackpressureTest, "BackyardMMO.Network.AcknowledgementCoalescingAndBarriers",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMNetworkBackpressureTest::RunTest(const FString& Parameters)
    {
        using namespace BYMNetworkStream;
        FSender Sender; FReceiver Receiver; FString Error;
        const FString Initial = Write(Rich());
        TestTrue(TEXT("Initial snapshot is queued"), Sender.Queue(Initial, false, false, Error));
        TestTrue(TEXT("Initial snapshot is prepared"), Sender.Prepare(true, true, Error));
        const int32 Pending = Sender.GetInFlightSequence();
        TestTrue(TEXT("A prepared frame is in flight"), Sender.HasInFlight());
        TestFalse(TEXT("Client cannot ACK a frame that has not been sent"), Sender.Acknowledge(Pending, true));
        TestFalse(TEXT("An unrelated sequence cannot advance the baseline"), Sender.Acknowledge(Pending + 1, true));
        FObject A = Parse(Initial); A->SetNumberField(TEXT("now"), 1001.); const FString First = Write(A);
        A->SetNumberField(TEXT("now"), 1002.); A->SetBoolField(TEXT("terminal"), true); const FString Terminal = Write(A);
        A->SetNumberField(TEXT("now"), 1003.); A->RemoveField(TEXT("terminal")); const FString Superseded = Write(A);
        A->SetNumberField(TEXT("now"), 1004.); const FString Latest = Write(A);
        TestTrue(TEXT("Slow-client ordinary update queues behind its ACK"), Sender.Queue(First, false, false, Error));
        TestTrue(TEXT("A terminal battle state forms an ordered barrier"), Sender.Queue(Terminal, false, true, Error));
        TestTrue(TEXT("New ordinary state can follow the terminal barrier"), Sender.Queue(Superseded, false, false, Error));
        TestTrue(TEXT("Latest ordinary state replaces only the ordinary tail"), Sender.Queue(Latest, false, false, Error));
        TestEqual(TEXT("The terminal barrier precedes the latest ordinary tail"), Sender.GetQueuedFrames(), 2);
        TestEqual(TEXT("Both superseded ordinary updates are coalesced"), Sender.GetCoalescedUpdates(), static_cast<uint64>(2));
        TestTrue(TEXT("Repeated Prepare while waiting is harmless"), Sender.Prepare(true, true, Error));
        TestEqual(TEXT("Waiting cannot allocate another in-flight frame"), Sender.GetInFlightSequence(), Pending);
        if (!Deliver(*this, Sender, Receiver, Initial) || !Deliver(*this, Sender, Receiver, Terminal)
            || !Deliver(*this, Sender, Receiver, Latest)) return false;
        TestTrue(TEXT("Unchanged follow-up can be queued"), Sender.Queue(Latest, false, false, Error));
        TestTrue(TEXT("Unchanged follow-up is considered successfully"), Sender.Prepare(true, true, Error));
        TestFalse(TEXT("Equal state emits no additional frame"), Sender.HasInFlight());
        TestEqual(TEXT("No-change suppression is counted"), Sender.GetSuppressedUpdates(), static_cast<uint64>(1));
        TestTrue(TEXT("Explicit view reset queues full state even when equal"), Sender.Queue(Latest, true, true, Error));
        if (!Deliver(*this, Sender, Receiver, Latest)) return false;
        Sender.Reset(); Receiver.Reset();
        TestFalse(TEXT("Reset discards old in-flight state"), Sender.HasInFlight());
        TestEqual(TEXT("Reset discards old receiver baseline"), Receiver.GetBaselineSequence(), 0);
        for (int32 Index = 0; Index < MaxQueuedFrames; ++Index)
            TestTrue(TEXT("Bounded command barriers fit within their limit"), Sender.Queue(FString::Printf(TEXT("{\"state\":%d}"), Index), false, true, Error));
        TestFalse(TEXT("A ninth barrier cannot create an unbounded reliable queue"), Sender.Queue(Initial, false, true, Error));
        TestEqual(TEXT("Queue overflow preserves the existing ordered barriers"), Sender.GetQueuedFrames(), MaxQueuedFrames);
        Sender.Reset();
        const FString Large = FString(TEXT("{\"text\":\"")) + FString::ChrN(BYMNetworkSnapshot::MaxDecodedCharacters - 32, TEXT('x')) + TEXT("\"}");
        TestTrue(TEXT("First large required frame fits the character budget"), Sender.Queue(Large, false, true, Error));
        TestTrue(TEXT("Second distinct large frame still fits"), Sender.Queue(Large + TEXT(" "), false, true, Error));
        TestFalse(TEXT("Character budget stops growth before the frame-count cap"), Sender.Queue(Large + TEXT("  "), false, true, Error));
        TestEqual(TEXT("Rejected character growth preserves both required states"), Sender.GetQueuedFrames(), 2);
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMNetworkRecoveryTest, "BackyardMMO.Network.ChunkValidationAndFullResynchronization",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMNetworkRecoveryTest::RunTest(const FString& Parameters)
    {
        using namespace BYMNetworkStream;
        FSender Sender; FReceiver Receiver; FString Error, Output = TEXT("old view");
        const FString Initial = Write(Rich());
        TestTrue(TEXT("Large raw initial frame queues"), Sender.Queue(Initial, false, true, Error));
        TestTrue(TEXT("Compression-off frame prepares for multi-chunk test"), Sender.Prepare(true, false, Error));
        TestTrue(TEXT("Fixture truly exercises multiple chunks"), Sender.GetPendingChunks() > 1);
        FChunk First; if (!TestTrue(TEXT("First chunk is available"), Sender.TakeNextChunk(First))) return false;
        FChunk Broken = First; Broken.RawBytes = -1;
        TestTrue(TEXT("Invalid first-chunk metadata is rejected"), Receiver.Accept(Broken, Output, Error) == EReceiveResult::Rejected);
        TestEqual(TEXT("Rejected partial state cannot replace the UI snapshot"), Output, FString(TEXT("old view")));
        TestTrue(TEXT("A negative ACK can cancel before all chunks were sent"), Sender.Acknowledge(First.Sequence, false));
        TestFalse(TEXT("The rejected frame cannot send remaining stale chunks"), Sender.HasChunksToSend());
        TestTrue(TEXT("Residual chunks for the rejected sequence are stale"), Receiver.Accept(First, Output, Error) == EReceiveResult::Stale);
        if (!Deliver(*this, Sender, Receiver, Initial, false)) return false;
        TestFalse(TEXT("The old rejected ACK cannot disturb the newer baseline"), Sender.Acknowledge(First.Sequence, false));
        FObject Change = Parse(Initial); Change->SetNumberField(TEXT("now"), 2000.); const FString Next = Write(Change);
        TestTrue(TEXT("Changed state queues after successful recovery"), Sender.Queue(Next, false, false, Error));
        TestTrue(TEXT("Changed state prepares a baseline delta"), Sender.Prepare(true, true, Error));
        FChunk Delta; if (!TestTrue(TEXT("Delta chunk is available"), Sender.TakeNextChunk(Delta))) return false;
        TestTrue(TEXT("Sparse fixture uses a delta after acknowledgement"), Delta.bDelta);
        Receiver.Reset(); Output = TEXT("reset view");
        TestTrue(TEXT("A receiver without a baseline rejects a delta"), Receiver.Accept(Delta, Output, Error) == EReceiveResult::Rejected);
        TestTrue(TEXT("Negative delta ACK requests an exact full retry"), Sender.Acknowledge(Delta.Sequence, false));
        TestTrue(TEXT("Full retry prepares"), Sender.Prepare(true, true, Error));
        FChunk Full; if (!TestTrue(TEXT("Retry chunk is available"), Sender.TakeNextChunk(Full))) return false;
        TestFalse(TEXT("Resynchronization cannot depend on the missing baseline"), Full.bDelta);
        TestEqual(TEXT("Full retry names no base sequence"), Full.BaseSequence, 0);
        TestTrue(TEXT("Full retry completes"), Receiver.Accept(Full, Output, Error) == EReceiveResult::Complete);
        TestTrue(TEXT("Full recovery contains every original gameplay value"), EqualJson(Output, Next));
        TestTrue(TEXT("Recovered full frame is acknowledged"), Sender.Acknowledge(Full.Sequence, true));
        TestTrue(TEXT("Duplicate completed frame is ignored"), Receiver.Accept(Full, Output, Error) == EReceiveResult::Stale);
        Receiver.Reset();
        Broken = First; Broken.Count = MaxChunks + 1;
        TestTrue(TEXT("Unbounded chunk count is rejected"), Receiver.Accept(Broken, Output, Error) == EReceiveResult::Rejected);
        Receiver.Reset(); Broken = First; Broken.Index = 1;
        TestTrue(TEXT("Missing first chunk is rejected"), Receiver.Accept(Broken, Output, Error) == EReceiveResult::Rejected);
        Receiver.Reset(); Broken = First; Broken.Bytes.SetNum(ChunkBytes + 1);
        TestTrue(TEXT("Oversized chunk cannot grow assembly memory"), Receiver.Accept(Broken, Output, Error) == EReceiveResult::Rejected);
        Receiver.Reset(); Broken = First; Broken.Sequence = MaxSequence + 1;
        TestTrue(TEXT("Out-of-range sequence is rejected"), Receiver.Accept(Broken, Output, Error) == EReceiveResult::Rejected);
        Receiver.Reset(); Output = TEXT("still old");
        TestTrue(TEXT("A valid initial chunk remains pending"), Receiver.Accept(First, Output, Error) == EReceiveResult::Pending);
        Broken = First; Broken.Index = 1; Broken.Sequence += 1;
        TestTrue(TEXT("Chunks from mixed frame sequences are rejected"), Receiver.Accept(Broken, Output, Error) == EReceiveResult::Rejected);
        TestEqual(TEXT("Invalid assembly never exposes a partial snapshot"), Output, FString(TEXT("still old")));
        Receiver.Reset();
        TestTrue(TEXT("A valid initial chunk starts a new assembly"), Receiver.Accept(First, Output, Error) == EReceiveResult::Pending);
        Broken = First; Broken.Index = 1; Broken.RawBytes += 1;
        TestTrue(TEXT("Metadata cannot change within a frame"), Receiver.Accept(Broken, Output, Error) == EReceiveResult::Rejected);
        Receiver.Reset(); Broken = Full; Broken.Sequence = MaxSequence;
        TestTrue(TEXT("Highest bounded sequence can be received"), Receiver.Accept(Broken, Output, Error) == EReceiveResult::Complete);
        Broken.Sequence = 1;
        TestTrue(TEXT("A full frame recovers sequence wraparound"), Receiver.Accept(Broken, Output, Error) == EReceiveResult::Complete);
        Broken.Sequence = MaxSequence;
        TestTrue(TEXT("A pre-wrap duplicate cannot roll state backwards"), Receiver.Accept(Broken, Output, Error) == EReceiveResult::Stale);
        Receiver.Reset(); Broken = Full; Broken.Sequence = 12;
        TestTrue(TEXT("A valid baseline precedes malformed-sequence recovery"), Receiver.Accept(Broken, Output, Error) == EReceiveResult::Complete);
        FChunk InvalidSequence = Broken; InvalidSequence.Sequence = 0;
        TestTrue(TEXT("An invalid sequence clears assembly safely"), Receiver.Accept(InvalidSequence, Output, Error) == EReceiveResult::Rejected);
        TestTrue(TEXT("Rejected invalid headers retain the old stale-frame watermark"), Receiver.Accept(Broken, Output, Error) == EReceiveResult::Stale);
        Broken.Sequence = 13;
        TestTrue(TEXT("A newer full frame can recover after an invalid header"), Receiver.Accept(Broken, Output, Error) == EReceiveResult::Complete);
        Sender.Reset(); Receiver.Reset();
        const FString UnicodeBoundary = FString(TEXT("{\"x\":\"")) + FString::ChrN(ChunkBytes - 8, TEXT('x')) + TEXT("\U0001f47e\"}");
        TestTrue(TEXT("Unicode crossing a raw byte chunk boundary queues"), Sender.Queue(UnicodeBoundary, true, true, Error));
        if (!Deliver(*this, Sender, Receiver, UnicodeBoundary, false)) return false;
        return true;
    }
}
#endif
