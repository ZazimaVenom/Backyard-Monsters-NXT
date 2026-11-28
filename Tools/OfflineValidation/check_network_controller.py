#!/usr/bin/env python3
"""Exercise actual controller transport methods with explicit engine/role/RPC substitutes.

This is a bounded integration test, not Unreal UHT/UBT, native replication,
Windows packaging, or a measurement of network-interface traffic.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / 'Plugins/BackyardMMO/Source/BackyardMMO'
ADAPTERS = Path(__file__).resolve().parent / 'Adapters'
CONTROLLER = RUNTIME / 'Private/BYMPlayerController.cpp'
HEADER = RUNTIME / 'Public/BYMPlayerController.h'
SETTINGS = RUNTIME / 'Public/BYMSettings.h'


def compact(value: str) -> str:
    return re.sub(r'\s+', '', value)


def source_parts() -> tuple[str, str, str, str]:
    source, header = CONTROLLER.read_text(), HEADER.read_text()
    start = source.index('void ABYMPlayerController::ResetNetworkState()')
    end = source.index('void ABYMPlayerController::BYMNetStats()', start)
    methods = source[start:end]
    signatures = re.findall(r'void ABYMPlayerController::(\w+)\((.*?)\)\s*\{', methods, re.S)
    declarations = '\n'.join('void ' + name + '(' + args + ');' for name, args in signatures)
    for name in ('ClientStateChunk', 'ServerStateAck', 'ServerStateChunkAck'):
        declaration = re.search(r'\bvoid ' + name + r'\((.*?)\);', header, re.S)
        definition = next((args for fn, args in signatures if fn == name + '_Implementation'), None)
        if declaration is None or definition is None or compact(declaration[1]) != compact(definition):
            raise RuntimeError('RPC declaration/implementation mismatch: ' + name)
    for name in ('ClientSnapshot', 'ClientSnapshotChunk', 'ClientWorldMap', 'ClientWorldMapChunk', 'ClientLadder', 'SendSnapshotPayload'):
        if re.search(r'\b' + name + r'(?:_Implementation)?\s*\(', source + header):
            raise RuntimeError('Old raw-JSON transfer path remains: ' + name)
    include_lines = re.findall(r'^#include\s+[^\n]+', header, re.M)
    if not include_lines or 'BYMPlayerController.generated.h' not in include_lines[-1]:
        raise RuntimeError('The Unreal generated header must remain the last include.')
    fields_start = header.index('    BYMNetworkStream::FSender NetworkSenders[3];')
    fields_end = header.index('    void ResetNetworkState();', fields_start)
    fields = header[fields_start:fields_end]
    values = re.findall(r'^\s*((?:FString|int32|float|bool)\s+\w+\s*=\s*[^;]+;)', SETTINGS.read_text(), re.M)
    settings = 'struct UBYMSettings {\n' + '\n'.join(values) + '\n};\n'
    return methods, declarations, fields, settings


PRELUDE = r'''
#include "CoreMinimal.h"
#include "BYMNetworkStream.h"
#include "Containers/StringConv.h"
#define UE_LOG(...) do {} while(false)
struct FPlatformTime { inline static double Now = 100; static double Seconds(){return Now;} };
struct FEnvelope { uint8 Stream; BYMNetworkStream::FChunk Chunk; };
struct FFrameAck { uint8 Stream; int32 Sequence; bool Accepted; };
struct FProgressAck { uint8 Stream; int32 Sequence; int32 Index; };
'''

CLASS_PREFIX = r'''
template<class T> T* GetMutableDefault(){static T Settings;return &Settings;}
template<class T> const T* GetDefault(){return GetMutableDefault<T>();}
class ABYMPlayerController {
public:
 bool Authority=true, Local=false, Menu=false, bAuthenticated=true;
 FString AuthenticatedId=TEXT("test-owner");
 FString StatusMessage;
 std::vector<FEnvelope> Wire;
 std::vector<FFrameAck> FrameAcks;
 std::vector<FProgressAck> ProgressAcks;
 std::vector<std::pair<uint8,FString>> Applied;
 int FailureCount=0, CommandBudgetCalls=0;
 bool HasAuthority()const{return Authority;}
 bool IsLocalController()const{return Local;}
 bool IsMenuWorld()const{return Menu;}
 bool AllowServerCall(int32,bool){++CommandBudgetCalls;return true;}
 void ClientStatus(const FString& Message){StatusMessage=Message;}
 void DisconnectAbusiveClient(const FString&){++FailureCount;}
 void ApplySnapshotJson(const FString& Json){Applied.emplace_back(0,Json);}
 void ApplyWorldMapJson(const FString& Json){Applied.emplace_back(1,Json);}
 void ApplyLadderJson(const FString& Json){Applied.emplace_back(2,Json);}
 void ClientStateChunk(uint8 StreamId,int32 Sequence,int32 BaseSequence,int32 Index,int32 Count,int32 RawBytes,bool bCompressed,bool bDelta,const TArray<uint8>& Bytes){
  BYMNetworkStream::FChunk Chunk;Chunk.Sequence=Sequence;Chunk.BaseSequence=BaseSequence;
  Chunk.Index=Index;Chunk.Count=Count;Chunk.RawBytes=RawBytes;Chunk.bCompressed=bCompressed;Chunk.bDelta=bDelta;Chunk.Bytes=Bytes;
  Wire.push_back({StreamId,MoveTemp(Chunk)});
 }
 void ServerStateAck(uint8 StreamId,int32 Sequence,bool bAccepted){FrameAcks.push_back({StreamId,Sequence,bAccepted});}
 void ServerStateChunkAck(uint8 StreamId,int32 Sequence,int32 Index){ProgressAcks.push_back({StreamId,Sequence,Index});}
'''

TESTS = r'''
static int Assertions=0;
#define EXPECT(value) do {++Assertions;if(!(value)){std::cerr<<"Assertion "<<Assertions<<" failed at line "<<__LINE__<<": "<<#value<<'\n';return 1;}}while(false)
static FString LargeJson(int32 value=1){return FString("{\"blob\":\"")+FString(40000,'q')+"\",\"value\":"+FString::FromInt(value)+"}";}
static void Receive(ABYMPlayerController& Client,const FEnvelope& Envelope){
 const auto& C=Envelope.Chunk;
 Client.ClientStateChunk_Implementation(Envelope.Stream,C.Sequence,C.BaseSequence,C.Index,C.Count,C.RawBytes,C.bCompressed,C.bDelta,C.Bytes);
}
static void Deliver(ABYMPlayerController& Server,ABYMPlayerController& Client){
 auto Messages=MoveTemp(Server.Wire);Server.Wire.clear();for(const auto& Message:Messages)Receive(Client,Message);
}
static void Acknowledge(ABYMPlayerController& Server,ABYMPlayerController& Client){
 auto Progress=MoveTemp(Client.ProgressAcks);Client.ProgressAcks.clear();
 for(const auto& A:Progress)Server.ServerStateChunkAck_Implementation(A.Stream,A.Sequence,A.Index);
 auto Frames=MoveTemp(Client.FrameAcks);Client.FrameAcks.clear();
 for(const auto& A:Frames)Server.ServerStateAck_Implementation(A.Stream,A.Sequence,A.Accepted);
}
static void Finish(ABYMPlayerController& Server,ABYMPlayerController& Client){
 for(int32 Step=0;Step<300;++Step){
  Server.FlushNetworkPayloads();Deliver(Server,Client);Acknowledge(Server,Client);
  bool More=false;for(const auto& S:Server.NetworkSenders)More=More||S.HasInFlight()||S.GetQueuedFrames()>0;
  if(!More)return;
 }
 throw std::runtime_error("Transfer failed to complete within bounded test steps");
}
static ABYMPlayerController Client(){ABYMPlayerController C;C.Authority=false;C.Local=true;C.ResetNetworkState();return C;}
int main(){
 UBYMSettings& Settings=*GetMutableDefault<UBYMSettings>();
 Settings.bCompressNetworkState=false;Settings.bUseNetworkDeltas=true;Settings.NetworkChunksPerTick=2;Settings.NetworkAckTimeoutSeconds=30;
 // Local host uses the same reconstructed-JSON entry points without a wire frame.
 ABYMPlayerController Host;Host.Local=true;Host.ResetNetworkState();
 for(uint8 Id=0;Id<3;++Id)Host.QueueNetworkPayload(Id,TEXT("{\"v\":1}"),true);
 EXPECT(Host.Applied.size()==3);EXPECT(Host.Wire.empty());EXPECT(Host.NetworkOfferedJsonBytes==0);
 Host.FlushNetworkPayloads();EXPECT(Host.Wire.empty());
 // Only authenticated authority may enqueue; bad channels never reach storage.
 ABYMPlayerController Guard;Guard.ResetNetworkState();Guard.Authority=false;
 Guard.QueueNetworkPayload(0,TEXT("{\"v\":1}"),false);EXPECT(Guard.NetworkSenders[0].GetQueuedFrames()==0);
 Guard.Authority=true;Guard.AuthenticatedId.Empty();Guard.QueueNetworkPayload(0,TEXT("{\"v\":1}"),false);EXPECT(Guard.NetworkSenders[0].GetQueuedFrames()==0);
 Guard.AuthenticatedId=TEXT("owner");Guard.QueueNetworkPayload(3,TEXT("{\"v\":1}"),false);EXPECT(Guard.Wire.empty());
 // Three independent streams share a dispatch budget; each grants at most one
 // chunk of credit until actual client progress ACK, even over repeated ticks.
 ABYMPlayerController Server;Server.ResetNetworkState();auto Receiver=Client();
 for(uint8 Id=0;Id<3;++Id)Server.QueueNetworkPayload(Id,LargeJson(),false);
 EXPECT(Server.Wire.empty());Server.FlushNetworkPayloads();EXPECT(Server.Wire.size()==2);
 EXPECT(Server.Wire[0].Stream!=Server.Wire[1].Stream);
 Server.FlushNetworkPayloads();EXPECT(Server.Wire.size()==3);
 Server.FlushNetworkPayloads();EXPECT(Server.Wire.size()==3);
 for(const auto& S:Server.NetworkSenders)EXPECT(S.HasInFlight());
 Deliver(Server,Receiver);EXPECT(Receiver.Applied.empty());EXPECT(Receiver.FrameAcks.empty());EXPECT(Receiver.ProgressAcks.size()==3);
 EXPECT(Receiver.NetworkReceivedBytes==3*BYMNetworkStream::ChunkBytes);
 const auto FirstAck=Receiver.ProgressAcks[0];
 Server.ServerStateChunkAck_Implementation(FirstAck.Stream,FirstAck.Sequence+1,FirstAck.Index);
 Server.ServerStateChunkAck_Implementation(FirstAck.Stream,FirstAck.Sequence,FirstAck.Index+1);
 Server.FlushNetworkPayloads();EXPECT(Server.Wire.empty());
 FPlatformTime::Now+=5;Acknowledge(Server,Receiver);EXPECT(Server.Wire.empty());
 EXPECT(Server.CommandBudgetCalls==0);EXPECT(Server.NetworkSenders[0].HasInFlight());
 Server.FlushNetworkPayloads();EXPECT(Server.Wire.size()==2);
 Deliver(Server,Receiver);Acknowledge(Server,Receiver);Finish(Server,Receiver);
 EXPECT(Receiver.Applied.size()==3);EXPECT(Receiver.NetworkReceivedFrames==3);
 for(int32 Id=0;Id<3;++Id){EXPECT(!Server.NetworkSenders[Id].HasInFlight());EXPECT(Server.NetworkReceivers[Id].GetBaselineSequence()==0);EXPECT(Receiver.NetworkReceivers[Id].GetBaselineSequence()>0);}
 // A frame ACK cannot promote a partially delivered frame; exact final ACK
 // advances only the baseline, with subsequent dispatch deferred to Tick.
 ABYMPlayerController Sequenced;Sequenced.ResetNetworkState();auto SmallClient=Client();
 Sequenced.QueueNetworkPayload(0,TEXT("{\"v\":1}"),false);Sequenced.FlushNetworkPayloads();EXPECT(Sequenced.Wire.size()==1);
 const int32 Seq=Sequenced.Wire[0].Chunk.Sequence;
 Sequenced.QueueNetworkPayload(0,TEXT("{\"v\":2}"),false);Sequenced.QueueNetworkPayload(0,TEXT("{\"v\":3}"),false);
 EXPECT(Sequenced.NetworkSenders[0].GetQueuedFrames()==1);EXPECT(Sequenced.NetworkSenders[0].GetCoalescedUpdates()>=1);
 Deliver(Sequenced,SmallClient);EXPECT(SmallClient.Applied.size()==1);EXPECT(SmallClient.FrameAcks.size()==1);
 Sequenced.ServerStateAck_Implementation(0,Seq+1,true);EXPECT(Sequenced.NetworkSenders[0].HasInFlight());
 Acknowledge(Sequenced,SmallClient);EXPECT(!Sequenced.NetworkSenders[0].HasInFlight());EXPECT(Sequenced.Wire.empty());
 Finish(Sequenced,SmallClient);EXPECT(SmallClient.Applied.size()==2);EXPECT(SmallClient.Applied.back().second.Contains(TEXT("\"v\":3")));
 EXPECT(Sequenced.CommandBudgetCalls==0);
 // Unauthenticated/nonlocal receivers do not acknowledge or apply state.
 ABYMPlayerController Protected;Protected.ResetNetworkState();Protected.QueueNetworkPayload(0,TEXT("{\"v\":1}"),false);Protected.FlushNetworkPayloads();
 auto Unauth=Client();Unauth.bAuthenticated=false;Receive(Unauth,Protected.Wire[0]);EXPECT(Unauth.Applied.empty());EXPECT(Unauth.FrameAcks.empty());
 Unauth.bAuthenticated=true;Unauth.Local=false;Receive(Unauth,Protected.Wire[0]);EXPECT(Unauth.Applied.empty());EXPECT(Unauth.FrameAcks.empty());
 // Reject a valid-sized first chunk with a nonexistent baseline, ACK rejection,
 // then require a new full sequence. Old fragments must not create duplicate ACKs.
 ABYMPlayerController Recover;Recover.ResetNetworkState();auto RecoverClient=Client();
 Recover.QueueNetworkPayload(0,LargeJson(),false);Recover.FlushNetworkPayloads();EXPECT(Recover.Wire.size()==1);
 auto Invalid=Recover.Wire[0];Recover.Wire.clear();Invalid.Chunk.bDelta=true;Invalid.Chunk.BaseSequence=7;
 Receive(RecoverClient,Invalid);EXPECT(RecoverClient.FrameAcks.size()==1);EXPECT(!RecoverClient.FrameAcks[0].Accepted);EXPECT(RecoverClient.Applied.empty());
 Acknowledge(Recover,RecoverClient);EXPECT(!Recover.NetworkSenders[0].HasInFlight());EXPECT(Recover.Wire.empty());
 Recover.FlushNetworkPayloads();EXPECT(Recover.Wire.size()==1);EXPECT(Recover.Wire[0].Chunk.Sequence!=Invalid.Chunk.Sequence);EXPECT(!Recover.Wire[0].Chunk.bDelta);
 Receive(RecoverClient,Invalid);EXPECT(RecoverClient.FrameAcks.empty());
 Deliver(Recover,RecoverClient);Acknowledge(Recover,RecoverClient);Finish(Recover,RecoverClient);
 EXPECT(RecoverClient.Applied.size()==1);EXPECT(Recover.NetworkSenders[0].GetConsecutiveRejects()==0);
 // Valid progress resets inactivity; total duration may exceed the timeout as
 // long as chunks are acknowledged. Without progress the stream fails once.
 ABYMPlayerController Slow;Slow.ResetNetworkState();auto SlowClient=Client();
 Slow.QueueNetworkPayload(0,LargeJson(),false);Slow.FlushNetworkPayloads();
 FPlatformTime::Now+=20;Deliver(Slow,SlowClient);Acknowledge(Slow,SlowClient);
 FPlatformTime::Now+=20;Slow.FlushNetworkPayloads();EXPECT(!Slow.bNetworkFailed);EXPECT(Slow.FailureCount==0);
 FPlatformTime::Now+=31;Slow.FlushNetworkPayloads();EXPECT(Slow.bNetworkFailed);EXPECT(Slow.FailureCount==1);
 const auto Before=Slow.Wire.size();Slow.QueueNetworkPayload(0,TEXT("{\"v\":999}"),true);Slow.FlushNetworkPayloads();EXPECT(Slow.Wire.size()==Before);EXPECT(Slow.FailureCount==1);
 // Required terminal states are queued in order and cannot be overwritten by
 // ordinary timer snapshots; they traverse the same controller apply path.
 ABYMPlayerController Barriers;Barriers.ResetNetworkState();auto BarrierClient=Client();
 Barriers.QueueNetworkPayload(0,TEXT("{\"battle\":1}"),true);Barriers.FlushNetworkPayloads();
 Barriers.QueueNetworkPayload(0,TEXT("{\"completedBattle\":1}"),true);Barriers.QueueNetworkPayload(0,TEXT("{\"idle\":1}"),false);
 Deliver(Barriers,BarrierClient);Acknowledge(Barriers,BarrierClient);Finish(Barriers,BarrierClient);
 EXPECT(BarrierClient.Applied.size()==3);EXPECT(BarrierClient.Applied[1].second.Contains(TEXT("completedBattle")));EXPECT(BarrierClient.Applied[2].second.Contains(TEXT("idle")));
 std::cout<<Assertions<<" controller assertions passed\n";
 return 0;
}
'''


def input_hashes() -> dict[str, str]:
    files = {Path(__file__).resolve(), CONTROLLER, HEADER, SETTINGS}
    files.update(ADAPTERS.rglob('*.h'))
    for name in ('BYMNetworkSnapshot', 'BYMNetworkStream'):
        files.add(RUNTIME / 'Public' / (name + '.h'))
        files.add(RUNTIME / 'Private' / (name + '.cpp'))
    return {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(files)}


def run(cxx: str) -> dict[str, object]:
    before=input_hashes()
    methods,declarations,fields,settings=source_parts()
    generated=PRELUDE+settings+CLASS_PREFIX+fields+'\n'+declarations+'\n};\n'+methods+TESTS
    with tempfile.TemporaryDirectory(prefix='bym-controller-contract-') as temp:
        cpp=Path(temp)/'controller.cpp';cpp.write_text(generated)
        binary=Path(temp)/'controller'
        command=[cxx,'-std=c++17','-O1','-Wall','-Wextra','-Wshadow','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation',
                 '-I'+str(ADAPTERS),'-I'+str(RUNTIME/'Public'),str(cpp),
                 str(RUNTIME/'Private/BYMNetworkSnapshot.cpp'),str(RUNTIME/'Private/BYMNetworkStream.cpp'),'-lz','-o',str(binary)]
        built=subprocess.run(command,capture_output=True,text=True)
        if built.returncode:raise RuntimeError('Actual controller-method compile failed:\n'+built.stderr[-12000:])
        executed=subprocess.run([str(binary)],capture_output=True,text=True)
        if executed.returncode:raise RuntimeError('Controller integration test failed:\n'+executed.stdout+executed.stderr)
    match=re.search(r'(\d+) controller assertions passed',executed.stdout)
    if not match:raise RuntimeError('No controller assertion summary')
    if input_hashes()!=before:raise RuntimeError('Controller validation inputs changed during the run; rerun after edits finish.')
    return {'result':'PASS','completed_at_utc':datetime.now(timezone.utc).isoformat(),
            'compiler':subprocess.run([cxx,'--version'],capture_output=True,text=True,check=True).stdout.splitlines()[0],
            'controller_assertions':int(match[1]),'rpc_declaration_checks':3,'old_raw_transfer_paths_absent':True,
            'scope':'Actual controller queue/dispatch/receive/ACK/reset methods and actual codec/stream helpers with substituted engine role, clock, settings and RPC sinks. Not Unreal UHT/UBT, actor replication, Windows packaging or wire traffic.',
            'sha256':before}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--cxx',default='g++');parser.add_argument('--record',action='store_true');args=parser.parse_args()
    report=json.dumps(run(args.cxx),indent=2)+'\n'
    if args.record:Path(__file__).with_name('network-controller-results.json').write_text(report)
    print(report,end='')
