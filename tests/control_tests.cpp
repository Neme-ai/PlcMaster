#include "plcworker.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QTextStream>
#include <cstring>
#include <vector>
static unsigned char memory[128]={};
static bool failFaultRead=false;
static bool failRead=false,failWrite=false,overrideWrite=false;
static int writes=0,badCalls=0;
static std::vector<int> edges;
extern "C" {
S7Object S7API Cli_Create(){return 1;}
void S7API Cli_Destroy(S7Object* p){*p=0;}
int S7API Cli_ConnectTo(S7Object,const char*,int,int){return 0;}
int S7API Cli_SetConnectionParams(S7Object,const char*,word,word){return 0;}
int S7API Cli_SetConnectionType(S7Object,word){return 0;}
int S7API Cli_Connect(S7Object){return 0;}
int S7API Cli_Disconnect(S7Object){return 0;}
int S7API Cli_MBRead(S7Object,int start,int size,void* data){if(failRead||(start==5&&failFaultRead))return 1;std::memcpy(data,memory+start,size);return 0;}
int S7API Cli_EBRead(S7Object c,int s,int n,void* d){return Cli_MBRead(c,s,n,d);}
int S7API Cli_ABRead(S7Object c,int s,int n,void* d){return Cli_MBRead(c,s,n,d);}
int S7API Cli_MBWrite(S7Object,int,int,void*){++badCalls;return 1;}
int S7API Cli_ABWrite(S7Object,int,int,void*){++badCalls;return 1;}
int S7API Cli_WriteArea(S7Object,int area,int db,int start,int count,int type,void* data){
    ++writes;if(area!=S7AreaMK||db!=0||count!=1||type!=S7WLBit||start<0||start>=1024){++badCalls;return 1;}
    if(failWrite)return 1;int value=*static_cast<unsigned char*>(data)&1;
    if(!overrideWrite) memory[start/8]=(memory[start/8]&~(1<<(start%8)))|(value<<(start%8));
    if(start==177)edges.push_back(value);return 0;
}
int S7API Cli_ErrorText(int,char* text,int size){strncpy_s(text,size,"simulated failure",_TRUNCATE);return 0;}
}
static void waitMs(int n){QEventLoop loop;QTimer::singleShot(n,&loop,&QEventLoop::quit);loop.exec();}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv); QFile file("test-results.txt");file.open(QIODevice::WriteOnly);QTextStream out(&file);int failures=0;
    auto check=[&](bool ok,const QString& name){out<<(ok?"PASS ":"FAIL ")<<name<<"\n";out.flush();if(!ok)++failures;};
    PlcWorker w; for(int b=20;b<=28;++b) for(int bit=0;bit<8;++bit)if(b==20||b==21||(b==22&&bit<=3)||b==25||b==26||b==27||(b==28&&bit<=2))w.addPoint(2,b,bit,"");
    int faultValue=-1;
    QObject::connect(&w,&PlcWorker::faultUpdated,[&](int value){faultValue=value;});
    double actualInterval=-2,readMs=-1; bool timingOk=false;
    QObject::connect(&w,&PlcWorker::pollingTiming,[&](double a,double b,bool ok){actualInterval=a;readMs=b;timingOk=ok;});
    bool result=false,running=false;QStringList logs;
    QObject::connect(&w,&PlcWorker::oneWriteResult,[&](bool ok,const QString&){result=ok;});
    QObject::connect(&w,&PlcWorker::pulseState,[&](bool on,const QString&){running=on;});
    QObject::connect(&w,&PlcWorker::actionLog,[&](const QString& s){logs<<s;});
    auto refresh=[&]{QMetaObject::invokeMethod(&w,"pollOnce",Qt::DirectConnection);};
    w.connectToPlc("offline simulator");int n=writes;w.writeOne("M20.0",1);check(!result&&writes==n,"default mode blocks writes");
    w.setWriteMode(true,false);memory[20]=0xaa;w.writeOne("M20.0",1);check(result&&memory[20]==0xab,"single bit set preserves seven neighbors");
    w.writeOne("M20.0",0);check(result&&memory[20]==0xaa,"single bit clear preserves seven neighbors");
    n=writes;w.writeOne("M25.0",1);check(!result&&writes==n,"feedback lock enforced in worker");
    w.setWriteMode(true,true);w.writeOne("M25.0",1);check(result&&(memory[25]&1),"feedback unlock permits targeted write");
    memory[5]=1;refresh();check(faultValue==1,"M5.0 fault high read independently");
    memory[5]=0;refresh();check(faultValue==0,"M5.0 fault low read independently");
    failFaultRead=true;refresh();check(faultValue==-1,"M5.0 read failure clears stale value");failFaultRead=false;
    n=writes;w.writeOne("M5.0",1);check(!result&&writes==n,"M5.0 remains read-only with feedback unlocked");
    n=writes;w.writeOne("M23.0",1);check(!result&&writes==n,"unlisted address rejected");
    w.writeOne("M21.5",1);waitMs(120);check(result&&(memory[21]&32),"ERR_RESET remains high, no auto pulse reset");
    overrideWrite=true;w.writeOne("M20.0",1);check(!result,"readback mismatch detected");overrideWrite=false;
    n=writes;w.writeOne("M20.0",1);check(writes==n,"mismatch revokes write permission");
    refresh();w.setWriteMode(true,false);edges.clear();memory[22]=0xa1;w.startPulse(200,80);waitMs(160);
    check(running&&(memory[22]&2),"pulse starts low then rises");
    memory[5]=1;refresh();check(running&&faultValue==1,"fault value does not stop pulses");
    failFaultRead=true;refresh();check(running&&faultValue==-1,"independent fault read failure does not stop pulses");failFaultRead=false;
    n=writes;w.writeOne("M20.1",1);check(result&&writes==n+1&&running,"other point writable while pulsing");
    w.writeOne("M22.2",1);check(result&&(memory[22]&2)&&(memory[22]&4)&&running,"same-byte write preserves high FeedingDog");
    w.writeOne("M22.2",0);check(result&&(memory[22]&2)&&!(memory[22]&4)&&running,"same-byte clear preserves high FeedingDog");
    n=writes;w.writeOne("M22.1",0);check(!result&&writes==n&&running,"FeedingDog manual clear blocked while pulsing");
    w.writeOne("M22.1",1);check(!result&&writes==n&&running,"FeedingDog manual set blocked while pulsing");
    w.setWriteMode(true,false);n=writes;w.writeOne("M25.0",0);check(!result&&writes==n&&running,"feedback lock still enforced while pulsing");
    w.setWriteMode(true,true);w.writeOne("M25.0",0);check(result&&running,"unlocked feedback writable while pulsing");
    w.stopPulse();check(!running&&memory[22]==0xa1,"stop during high clears only FeedingDog, preserves EnableDog and neighbors");
    n=writes;waitMs(250);check(writes==n,"stop cancels pending pulse edges");
    refresh();w.startPulse(200,80);waitMs(550);w.stopPulse();
    bool alternates=edges.size()>5;for(size_t i=1;i<edges.size();++i)if(edges[i]==1&&edges[i-1]==1)alternates=false;
    check(alternates,"multiple alternating pulse cycles");
    n=writes;w.startPulse(100,100);check(!running&&writes==n,"invalid pulse settings blocked");
    refresh();w.startPulse(5,2);check(running,"5 ms period with 2 ms high accepted");w.stopPulse();
    n=writes;w.startPulse(4,2);check(!running&&writes==n,"period below 5 ms rejected");
    n=writes;w.startPulse(5,0);check(!running&&writes==n,"zero high duration rejected");
    n=writes;w.startPulse(5,5);check(!running&&writes==n,"zero low duration rejected");
    refresh();w.setWriteMode(true,false);w.startPulse(200,80);waitMs(150);failRead=true;refresh();
    check(!running&&!(memory[22]&2),"read failure stops pulse and attempts low even when readback fails");failRead=false;
    refresh();n=writes;w.writeOne("M20.0",1);check(writes==n,"read recovery never automatically reenables writes");
    w.setWriteMode(true,false);failWrite=true;w.writeOne("M20.0",1);check(!result,"write transport failure surfaced");failWrite=false;
    refresh();w.setWriteMode(true,false);w.startPulse(200,80);waitMs(150);w.disconnectPlc();
    check(!running&&!(memory[22]&2),"disconnect during high performs low cleanup");
    check(faultValue==-1,"disconnect invalidates fixed fault indicator");
    w.connectToPlc("offline simulator");n=writes;waitMs(250);w.writeOne("M20.0",1);check(writes==n,"reconnect never resumes pulses or retries commands");
    check(badCalls==0,"all mutations use S7WLBit with bit offsets, no byte writes");
    check(logs.join("\n").contains(QStringLiteral("原值="))&&logs.join("\n").contains(QStringLiteral("读回=")),"operation log contains old, target and readback");
    w.setInterval(5); refresh(); check(actualInterval<0&&readMs>=0&&timingOk,"first timing sample after interval change has no invented interval");
    waitMs(12); refresh(); check(actualInterval>=10&&readMs>=0,"actual interval measured with monotonic clock");
    failRead=true;refresh();check(!timingOk&&readMs>=0,"failed reads publish timing with failure status");failRead=false;
    w.disconnectPlc();out<<"Failures: "<<failures<<"\n";return failures?1:0;
}



