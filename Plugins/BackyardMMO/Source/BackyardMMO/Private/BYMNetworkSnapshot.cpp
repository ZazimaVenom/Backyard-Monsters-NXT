#include "BYMNetworkSnapshot.h"

#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Compression.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace BYMNetworkSnapshot
{
namespace
{
    using FObject = TSharedPtr<FJsonObject>;
    using FValue = TSharedPtr<FJsonValue>;
    using FArray = TArray<FValue>;

    bool Fail(FString& Error, const TCHAR* Message)
    {
        Error = Message;
        return false;
    }

    // Bound nesting before invoking the engine JSON reader, not afterwards.
    bool Preflight(const FString& Text, int32 DepthLimit)
    {
        if (Text.IsEmpty() || Text.Len() > MaxDecodedCharacters) return false;
        int32 Depth = 0;
        bool bString = false, bEscape = false;
        for (int32 Index = 0; Index < Text.Len(); ++Index)
        {
            const TCHAR Character = Text[Index];
            if (Character == 0) return false;
            if (bString)
            {
                if (bEscape) bEscape = false;
                else if (Character == TEXT('\\')) bEscape = true;
                else if (Character == TEXT('"')) bString = false;
            }
            else if (Character == TEXT('"')) bString = true;
            else if (Character == TEXT('{') || Character == TEXT('['))
            {
                if (++Depth > DepthLimit) return false;
            }
            else if (Character == TEXT('}') || Character == TEXT(']'))
            {
                if (--Depth < 0) return false;
            }
        }
        return Depth == 0 && !bString && !bEscape;
    }

    bool Validate(const FValue& Value, int32 Depth, int32& Nodes, int32 DepthLimit, int32 NodeLimit)
    {
        if (!Value.IsValid() || Depth > DepthLimit || ++Nodes > NodeLimit) return false;
        switch (Value->Type)
        {
        case EJson::Null: case EJson::Boolean: case EJson::String: return true;
        case EJson::Number: return FMath::IsFinite(Value->AsNumber());
        case EJson::Object:
        {
            const FObject Object = Value->AsObject();
            if (!Object.IsValid()) return false;
            for (const auto& Pair : Object->Values)
            {
                // Runtime keys are ordinary identifiers. Reject embedded NULs
                // before any null-terminated key conversion can truncate one.
                const FString Key(*Pair.Key);
                if (Key.Len() != Pair.Key.Len()
                    || !Validate(Pair.Value, Depth + 1, Nodes, DepthLimit, NodeLimit)) return false;
            }
            return true;
        }
        case EJson::Array:
        {
            const FArray& Array = Value->AsArray();
            if (Array.Num() > MaxArrayEntries) return false;
            for (const FValue& Entry : Array)
                if (!Validate(Entry, Depth + 1, Nodes, DepthLimit, NodeLimit)) return false;
            return true;
        }
        default: return false;
        }
    }

    bool Parse(const FString& Text, FObject& Object, bool bPatch = false)
    {
        const int32 DepthLimit = bPatch ? MaxDepth * 2 + 4 : MaxDepth;
        if (!Preflight(Text, DepthLimit)) return false;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object) || !Object.IsValid()) return false;
        int32 Nodes = 0;
        return Validate(MakeShared<FJsonValueObject>(Object), 1, Nodes, DepthLimit, bPatch ? MaxNodes * 3 : MaxNodes);
    }

    bool Write(const FObject& Object, FString& Text)
    {
        const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
        return FJsonSerializer::Serialize(Object.ToSharedRef(), Writer) && Text.Len() <= MaxDecodedCharacters;
    }

    bool Equal(const FValue& A, const FValue& B)
    {
        if (!A.IsValid() || !B.IsValid()) return !A.IsValid() && !B.IsValid();
        if (A->Type != B->Type) return false;
        switch (A->Type)
        {
        case EJson::Null: return true;
        case EJson::Boolean: return A->AsBool() == B->AsBool();
        case EJson::Number: return A->AsNumber() == B->AsNumber();
        case EJson::String: return A->AsString() == B->AsString();
        case EJson::Array:
        {
            const FArray& Left = A->AsArray(); const FArray& Right = B->AsArray();
            if (Left.Num() != Right.Num()) return false;
            for (int32 Index = 0; Index < Left.Num(); ++Index)
                if (!Equal(Left[Index], Right[Index])) return false;
            return true;
        }
        case EJson::Object:
        {
            const FObject Left = A->AsObject(), Right = B->AsObject();
            if (Left->Values.Num() != Right->Values.Num()) return false;
            for (const auto& Pair : Left->Values)
            {
                const FString Key(*Pair.Key);
                if (!Equal(Pair.Value, Right->TryGetField(Key))) return false;
            }
            return true;
        }
        default: return false;
        }
    }

    // Non-cryptographic identity for a baseline. Object insertion order and
    // whitespace do not affect it. Type markers and lengths avoid ambiguity.
    void HashByte(uint64& Hash, uint8 Byte) { Hash = (Hash ^ Byte) * 1099511628211ull; }
    void HashNumber(uint64& Hash, uint64 Number)
    {
        for (int32 Shift = 0; Shift < 64; Shift += 8) HashByte(Hash, static_cast<uint8>(Number >> Shift));
    }
    void HashString(uint64& Hash, const FString& Text)
    {
        const FTCHARToUTF8 UTF8(*Text, Text.Len());
        HashNumber(Hash, static_cast<uint64>(UTF8.Length()));
        for (int32 Index = 0; Index < UTF8.Length(); ++Index) HashByte(Hash, static_cast<uint8>(UTF8.Get()[Index]));
    }
    void HashValue(uint64& Hash, const FValue& Value)
    {
        switch (Value->Type)
        {
        case EJson::Null: HashByte(Hash, 'N'); break;
        case EJson::Boolean: HashByte(Hash, Value->AsBool() ? 'T' : 'F'); break;
        case EJson::String: HashByte(Hash, 'S'); HashString(Hash, Value->AsString()); break;
        case EJson::Number:
        {
            HashByte(Hash, 'D');
            const double Number = Value->AsNumber() == 0. ? 0. : Value->AsNumber();
            uint64 Bits = 0; static_assert(sizeof(Bits) == sizeof(Number), "Expected IEEE double width");
            FMemory::Memcpy(&Bits, &Number, sizeof(Bits));
            HashNumber(Hash, Bits);
            break;
        }
        case EJson::Array:
            HashByte(Hash, 'A'); HashNumber(Hash, static_cast<uint64>(Value->AsArray().Num()));
            for (const FValue& Entry : Value->AsArray()) HashValue(Hash, Entry);
            break;
        case EJson::Object:
        {
            HashByte(Hash, 'O');
            const FObject Object = Value->AsObject();
            TArray<FString> Keys; Keys.Reserve(Object->Values.Num());
            for (const auto& Pair : Object->Values) Keys.Add(FString(*Pair.Key));
            Keys.Sort(); HashNumber(Hash, static_cast<uint64>(Keys.Num()));
            for (const FString& Key : Keys) { HashString(Hash, Key); HashValue(Hash, Object->TryGetField(Key)); }
            break;
        }
        default: break; // Parsed/validated values cannot reach this branch.
        }
    }
    FString Fingerprint(const FObject& Object)
    {
        uint64 Hash = 14695981039346656037ull;
        HashValue(Hash, MakeShared<FJsonValueObject>(Object));
        FString Text;
        for (int32 Shift = 60; Shift >= 0; Shift -= 4) Text += TEXT("0123456789abcdef")[(Hash >> Shift) & 15];
        return Text;
    }

    // Every patch is explicit: {v:value} replaces, {o:{key:patch},r:[key]}
    // edits an object, and {a:{index:patch},n:length} edits/resizes an array.
    // An absent field is distinct from a present JSON null.
    FObject Difference(const FValue& Before, const FValue& After)
    {
        if (Equal(Before, After)) return nullptr;
        const FObject Patch = MakeShared<FJsonObject>();
        if (Before.IsValid() && Before->Type == After->Type && After->Type == EJson::Object)
        {
            const FObject Old = Before->AsObject(), Current = After->AsObject(), Changed = MakeShared<FJsonObject>();
            FArray Removed;
            for (const auto& Pair : Current->Values)
            {
                const FString Key(*Pair.Key);
                const FObject Child = Difference(Old->TryGetField(Key), Pair.Value);
                if (Child.IsValid()) Changed->SetObjectField(Key, Child);
            }
            for (const auto& Pair : Old->Values)
            {
                const FString Key(*Pair.Key);
                if (!Current->HasField(Key)) Removed.Add(MakeShared<FJsonValueString>(Key));
            }
            Patch->SetObjectField(TEXT("o"), Changed);
            Patch->SetArrayField(TEXT("r"), Removed);
        }
        else if (Before.IsValid() && Before->Type == After->Type && After->Type == EJson::Array)
        {
            const FArray& Old = Before->AsArray(); const FArray& Current = After->AsArray();
            const FObject Changed = MakeShared<FJsonObject>();
            for (int32 Index = 0; Index < Current.Num(); ++Index)
            {
                const FValue OldValue = Old.IsValidIndex(Index) ? Old[Index] : FValue();
                const FObject Child = Difference(OldValue, Current[Index]);
                if (Child.IsValid()) Changed->SetObjectField(FString::FromInt(Index), Child);
            }
            Patch->SetObjectField(TEXT("a"), Changed);
            Patch->SetNumberField(TEXT("n"), Current.Num());
        }
        else Patch->SetField(TEXT("v"), After);
        return Patch;
    }

    bool ArrayIndex(const FString& Text, int32 Limit, int32& Result)
    {
        if (Text.IsEmpty() || Text.Len() > 6 || (Text.Len() > 1 && Text[0] == TEXT('0'))) return false;
        int32 Number = 0;
        for (int32 Index = 0; Index < Text.Len(); ++Index)
        {
            if (Text[Index] < TEXT('0') || Text[Index] > TEXT('9')) return false;
            Number = Number * 10 + Text[Index] - TEXT('0');
        }
        Result = Number;
        return Number < Limit;
    }

    bool Apply(const FValue& Before, const FObject& Patch, FValue& After, int32 Depth, int32& Operations, int32& AllocatedEntries)
    {
        if (!Patch.IsValid() || Depth > MaxDepth || ++Operations > MaxNodes) return false;
        const FValue Replacement = Patch->TryGetField(TEXT("v"));
        if (Replacement.IsValid())
        {
            if (Patch->Values.Num() != 1) return false;
            After = Replacement;
            return true;
        }
        const FValue ObjectEdits = Patch->TryGetField(TEXT("o")), ArrayEdits = Patch->TryGetField(TEXT("a"));
        if (ObjectEdits.IsValid())
        {
            const FValue Removes = Patch->TryGetField(TEXT("r"));
            if (Patch->Values.Num() != 2 || ObjectEdits->Type != EJson::Object
                || !Removes.IsValid() || Removes->Type != EJson::Array
                || !Before.IsValid() || Before->Type != EJson::Object) return false;
            const FObject Old = Before->AsObject(), Edits = ObjectEdits->AsObject(), Result = MakeShared<FJsonObject>();
            TSet<FString> Removed;
            for (const FValue& Entry : Removes->AsArray())
            {
                if (!Entry.IsValid() || Entry->Type != EJson::String) return false;
                const FString Key = Entry->AsString();
                if (!Old->HasField(Key) || Edits->HasField(Key) || Removed.Contains(Key)) return false;
                Removed.Add(Key);
            }
            const int32 RetainedEntries = Old->Values.Num() - Removed.Num();
            if (RetainedEntries > MaxNodes - AllocatedEntries) return false;
            AllocatedEntries += RetainedEntries;
            // Reuse immutable unchanged values. Nothing in the parsed baseline
            // or OutJson is mutated, even if a later child patch is invalid.
            for (const auto& Pair : Old->Values)
            {
                const FString Key(*Pair.Key);
                if (!Removed.Contains(Key)) Result->SetField(Key, Pair.Value);
            }
            for (const auto& Pair : Edits->Values)
            {
                const FString Key(*Pair.Key);
                FValue Child;
                if (!Old->HasField(Key) && ++AllocatedEntries > MaxNodes) return false;
                if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Object
                    || !Apply(Old->TryGetField(Key), Pair.Value->AsObject(), Child, Depth + 1, Operations, AllocatedEntries)) return false;
                Result->SetField(Key, Child);
            }
            After = MakeShared<FJsonValueObject>(Result);
            return true;
        }
        if (ArrayEdits.IsValid())
        {
            const FValue LengthValue = Patch->TryGetField(TEXT("n"));
            if (Patch->Values.Num() != 2 || ArrayEdits->Type != EJson::Object
                || !LengthValue.IsValid() || LengthValue->Type != EJson::Number
                || !Before.IsValid() || Before->Type != EJson::Array) return false;
            const double LengthNumber = LengthValue->AsNumber();
            if (LengthNumber < 0 || LengthNumber > MaxArrayEntries || LengthNumber != static_cast<int32>(LengthNumber)) return false;
            const int32 Length = static_cast<int32>(LengthNumber);
            const FArray& Old = Before->AsArray();
            const FObject Edits = ArrayEdits->AsObject();
            // Every appended entry needs an explicit patch. Reject sparse
            // growth before allocating it, including nested expansion attacks.
            if (Length > Old.Num() && Length - Old.Num() > Edits->Values.Num()) return false;
            if (Length > MaxNodes - AllocatedEntries) return false;
            AllocatedEntries += Length;
            FArray Result; Result.SetNum(Length);
            for (int32 Index = 0; Index < FMath::Min(Old.Num(), Length); ++Index) Result[Index] = Old[Index];
            for (const auto& Pair : Edits->Values)
            {
                const FString Key(*Pair.Key);
                int32 Index = 0;
                if (!ArrayIndex(Key, Length, Index) || !Pair.Value.IsValid() || Pair.Value->Type != EJson::Object) return false;
                FValue Child;
                const FValue OldValue = Old.IsValidIndex(Index) ? Old[Index] : FValue();
                if (!Apply(OldValue, Pair.Value->AsObject(), Child, Depth + 1, Operations, AllocatedEntries)) return false;
                Result[Index] = Child;
            }
            for (const FValue& Entry : Result) if (!Entry.IsValid()) return false;
            After = MakeShared<FJsonValueArray>(Result);
            return true;
        }
        return false;
    }

    bool ValidUTF8(const TArray<uint8>& Bytes)
    {
        int32 Index = 0;
        while (Index < Bytes.Num())
        {
            const uint8 Lead = Bytes[Index++];
            if (Lead == 0) return false;
            if (Lead < 0x80) continue;
            int32 Count = 0; uint32 Code = 0, Minimum = 0;
            if (Lead >= 0xc2 && Lead <= 0xdf) { Count = 1; Code = Lead & 0x1f; Minimum = 0x80; }
            else if (Lead >= 0xe0 && Lead <= 0xef) { Count = 2; Code = Lead & 0x0f; Minimum = 0x800; }
            else if (Lead >= 0xf0 && Lead <= 0xf4) { Count = 3; Code = Lead & 7; Minimum = 0x10000; }
            else return false;
            if (Count > Bytes.Num() - Index) return false;
            for (int32 Part = 0; Part < Count; ++Part)
            {
                const uint8 Next = Bytes[Index++];
                if ((Next & 0xc0) != 0x80) return false;
                Code = (Code << 6) | (Next & 0x3f);
            }
            if (Code < Minimum || Code > 0x10ffff || (Code >= 0xd800 && Code <= 0xdfff)) return false;
        }
        return true;
    }

    bool Pack(const FString& Json, bool bCompression, bool bDelta, FEncoded& Result)
    {
        const FTCHARToUTF8 UTF8(*Json, Json.Len());
        if (UTF8.Length() <= 0 || UTF8.Length() > MaxDecodedBytes) return false;
        Result = FEncoded();
        Result.UncompressedBytes = UTF8.Length();
        Result.bHasUpdate = true;
        Result.bDelta = bDelta;
        Result.Bytes.Append(reinterpret_cast<const uint8*>(UTF8.Get()), UTF8.Length());
        if (bCompression && UTF8.Length() >= 256)
        {
            int64 Bound = 0;
            if (FCompression::CompressMemoryBound(NAME_Zlib, Bound, static_cast<int64>(UTF8.Length()), uintptr_t(0))
                && Bound > 0 && Bound <= static_cast<int64>(MaxDecodedBytes) + 65536)
            {
                TArray<uint8> Compressed; Compressed.SetNumUninitialized(static_cast<int32>(Bound));
                int64 CompressedBytes = Bound;
                if (FCompression::CompressMemory(NAME_Zlib, Compressed.GetData(), CompressedBytes, UTF8.Get(),
                    static_cast<int64>(UTF8.Length()), COMPRESS_BiasSpeed, uintptr_t(0))
                    && CompressedBytes > 0 && CompressedBytes <= Bound && CompressedBytes + 16 < UTF8.Length())
                {
                    Compressed.SetNum(static_cast<int32>(CompressedBytes));
                    Result.Bytes = MoveTemp(Compressed);
                    Result.bCompressed = true;
                }
            }
        }
        return true;
    }
}

bool Encode(const FString& CurrentJson, const FString& BaselineJson, bool bAllowDelta, bool bAllowCompression,
    FEncoded& Out, FString& Error)
{
    Out = FEncoded(); Error.Empty();
    FObject Current;
    if (!Parse(CurrentJson, Current)) return Fail(Error, TEXT("Invalid or oversized network snapshot."));
    FObject Baseline;
    const bool bHaveBaseline = bAllowDelta && !BaselineJson.IsEmpty() && Parse(BaselineJson, Baseline);
    const FValue CurrentValue = MakeShared<FJsonValueObject>(Current);
    const FValue BaselineValue = bHaveBaseline ? FValue(MakeShared<FJsonValueObject>(Baseline)) : FValue();
    if (bHaveBaseline && Equal(BaselineValue, CurrentValue)) return true;
    FString FullJson;
    FEncoded Full;
    if (!Write(Current, FullJson) || !Pack(FullJson, bAllowCompression, false, Full))
        return Fail(Error, TEXT("Network snapshot exceeds the payload budget."));
    if (bHaveBaseline)
    {
        const FObject Patch = Difference(BaselineValue, CurrentValue);
        const FObject Envelope = MakeShared<FJsonObject>();
        Envelope->SetNumberField(TEXT("version"), 1);
        Envelope->SetStringField(TEXT("base"), Fingerprint(Baseline));
        Envelope->SetObjectField(TEXT("patch"), Patch);
        FString DeltaJson;
        FEncoded Delta;
        if (Write(Envelope, DeltaJson) && Pack(DeltaJson, bAllowCompression, true, Delta)
            && Delta.Bytes.Num() < Full.Bytes.Num())
        {
            Out = MoveTemp(Delta);
            return true;
        }
    }
    Out = MoveTemp(Full);
    return true;
}

bool Decode(const FEncoded& Encoded, const FString& BaselineJson, FString& OutJson, FString& Error)
{
    Error.Empty();
    if (!Encoded.bHasUpdate || Encoded.Bytes.Num() <= 0 || Encoded.Bytes.Num() > MaxDecodedBytes
        || Encoded.UncompressedBytes <= 0 || Encoded.UncompressedBytes > MaxDecodedBytes)
        return Fail(Error, TEXT("Invalid network frame size."));
    TArray<uint8> Raw;
    if (Encoded.bCompressed)
    {
        // Zero initialization also makes any incorrectly short decompression
        // fail UTF-8 validation instead of examining uninitialized bytes.
        Raw.Init(0, Encoded.UncompressedBytes);
        if (!FCompression::UncompressMemory(NAME_Zlib, Raw.GetData(), static_cast<int64>(Raw.Num()),
            Encoded.Bytes.GetData(), static_cast<int64>(Encoded.Bytes.Num()), COMPRESS_NoFlags, uintptr_t(0)))
            return Fail(Error, TEXT("Invalid compressed network frame."));
    }
    else
    {
        if (Encoded.Bytes.Num() != Encoded.UncompressedBytes) return Fail(Error, TEXT("Network frame length mismatch."));
        Raw = Encoded.Bytes;
    }
    if (!ValidUTF8(Raw)) return Fail(Error, TEXT("Invalid network UTF-8 text."));
    const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Raw.GetData()), Raw.Num());
    if (Converted.Length() > MaxDecodedCharacters) return Fail(Error, TEXT("Decoded network text exceeds its budget."));
    const FString Text(Converted.Length(), Converted.Get());
    FObject Document;
    if (!Parse(Text, Document, Encoded.bDelta)) return Fail(Error, TEXT("Invalid network JSON frame."));
    FObject Result = Document;
    if (Encoded.bDelta)
    {
        const FValue Version = Document->TryGetField(TEXT("version"));
        const FValue Base = Document->TryGetField(TEXT("base"));
        const FValue Patch = Document->TryGetField(TEXT("patch"));
        if (Document->Values.Num() != 3 || !Version.IsValid() || Version->Type != EJson::Number || Version->AsNumber() != 1.
            || !Base.IsValid() || Base->Type != EJson::String || Base->AsString().Len() != 16
            || !Patch.IsValid() || Patch->Type != EJson::Object)
            return Fail(Error, TEXT("Invalid network change envelope."));
        FObject Baseline;
        if (!Parse(BaselineJson, Baseline) || Fingerprint(Baseline) != Base->AsString())
            return Fail(Error, TEXT("Network changes do not match the received baseline."));
        FValue Changed;
        int32 Operations = 0, Nodes = 0, AllocatedEntries = 0;
        if (!Apply(MakeShared<FJsonValueObject>(Baseline), Patch->AsObject(), Changed, 1, Operations, AllocatedEntries)
            || !Changed.IsValid() || Changed->Type != EJson::Object
            || !Validate(Changed, 1, Nodes, MaxDepth, MaxNodes))
            return Fail(Error, TEXT("Invalid network change operations."));
        Result = Changed->AsObject();
    }
    FString Candidate;
    if (!Write(Result, Candidate)) return Fail(Error, TEXT("Reconstructed network snapshot exceeds its budget."));
    const FTCHARToUTF8 ResultBytes(*Candidate, Candidate.Len());
    if (ResultBytes.Length() > MaxDecodedBytes) return Fail(Error, TEXT("Reconstructed network bytes exceed their budget."));
    OutJson = MoveTemp(Candidate);
    return true;
}
}
