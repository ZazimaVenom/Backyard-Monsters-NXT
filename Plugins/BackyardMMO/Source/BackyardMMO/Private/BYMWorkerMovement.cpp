#include "BYMWorkerMovement.h"
#include <queue>
#include <vector>

namespace BYMWorkerMovement
{
    namespace
    {
        bool Finite(const FVector2D& P) { return FMath::IsFinite(P.X)&&FMath::IsFinite(P.Y); }
        double ScreenDistance(const FVector2D& D)
        { return FVector2D(D.X-D.Y,(D.X+D.Y)*.5).Size()*20.; }
        double BounceOut(double T)
        {
            if(T<1./2.75)return 7.5625*T*T;
            if(T<2./2.75){T-=1.5/2.75;return 7.5625*T*T+.75;}
            if(T<2.5/2.75){T-=2.25/2.75;return 7.5625*T*T+.9375;}
            T-=2.625/2.75;return 7.5625*T*T+.984375;
        }
        // Original .05px/display-tick acceleration and 2px/tick task speed at
        // 40Hz. A compact route integrates this ramp analytically; it does not
        // reproduce the source's +/- .05px terminal-speed oscillation.
        double TimeAtDistance(double Distance)
        { return Distance<=40.?FMath::Sqrt(FMath::Max(0.,Distance)/40.):1.+(Distance-40.)/80.; }
        struct FOpen
        {
            int32 Index;
            double Cost;
            bool operator<(const FOpen& Other)const
            { return Cost==Other.Cost?Index>Other.Index:Cost>Other.Cost; }
        };
    }

    bool PlanRoute(const FVector2D& Start,const FVector2D& Target,const FIntRect& Bounds,
        const TArray<FIntRect>& Occupied,TArray<FRoutePoint>& OutRoute,double& OutDuration)
    {
        OutRoute.Reset();OutDuration=0.;
        if(!Finite(Start)||!Finite(Target)||Bounds.Width()<1||Bounds.Height()<1
            ||Bounds.Width()>256||Bounds.Height()>256)return false;
        // Original mushrooms can live 200 source pixels outside the yard. The
        // extra ring also supports old worker positions after save migration.
        const int32 MinX=Bounds.Min.X-20,MinY=Bounds.Min.Y-20;
        const int32 Width=Bounds.Width()+40,Height=Bounds.Height()+40;
        const auto Inside=[&](const FVector2D& P)
        {return P.X>=MinX&&P.Y>=MinY&&P.X<MinX+Width&&P.Y<MinY+Height;};
        if(!Inside(Start)||!Inside(Target))return false;
        const auto IndexOf=[&](const FVector2D& P)
        {return (FMath::FloorToInt(P.Y)-MinY)*Width+FMath::FloorToInt(P.X)-MinX;};
        const auto Center=[&](int32 I)
        {return FVector2D(MinX+I%Width+.5,MinY+I/Width+.5);};
        const int32 Count=Width*Height,First=IndexOf(Start),Last=IndexOf(Target);
        TArray<uint8> Blocked;Blocked.Init(0,Count);
        for(const FIntRect& R:Occupied)
            for(int32 Y=FMath::Max(MinY,R.Min.Y);Y<FMath::Min(MinY+Height,R.Max.Y);++Y)
                for(int32 X=FMath::Max(MinX,R.Min.X);X<FMath::Min(MinX+Width,R.Max.X);++X)
                    Blocked[(Y-MinY)*Width+X-MinX]=1;
        TArray<int32> Parents;Parents.Init(INDEX_NONE,Count);
        TArray<double> Cost;Cost.Init(TNumericLimits<double>::Max(),Count);
        std::priority_queue<FOpen> Open;
        Cost[First]=0.;Parents[First]=First;Open.push({First,0.});
        while(!Open.empty())
        {
            const FOpen Current=Open.top();Open.pop();
            if(Current.Cost!=Cost[Current.Index])continue;
            if(Current.Index==Last)break;
            const int32 X=Current.Index%Width,Y=Current.Index/Width;
            for(int32 DY=-1;DY<=1;++DY)for(int32 DX=-1;DX<=1;++DX)
            {
                if((DX==0&&DY==0)||X+DX<0||Y+DY<0||X+DX>=Width||Y+DY>=Height)continue;
                const int32 Next=(Y+DY)*Width+X+DX;
                const double NextCost=Current.Cost+(Blocked[Next]?20.:10.)*(DX!=0&&DY!=0?1.5:1.);
                if(NextCost>=Cost[Next])continue;
                Cost[Next]=NextCost;Parents[Next]=Current.Index;Open.push({Next,NextCost});
            }
        }
        if(Parents[Last]==INDEX_NONE)return false;
        TArray<int32> Reverse;
        for(int32 I=Last;I!=First;I=Parents[I])Reverse.Add(I);
        FRoutePoint Initial;Initial.Position=Start;OutRoute.Add(Initial);
        for(int32 I=Reverse.Num()-1;I>=0;--I)
        {
            FRoutePoint P;P.Position=I==0?Target:Center(Reverse[I]);
            if((P.Position-OutRoute.Last().Position).SizeSquared()>.000001)OutRoute.Add(P);
        }
        // Preserve the exact authoritative destination even for subpixel jobs;
        // otherwise a valid freshly written save could fail its endpoint check.
        if((Target-OutRoute.Last().Position).SizeSquared()>0.)
        {FRoutePoint P;P.Position=Target;OutRoute.Add(P);}
        double Distance=0.,LastHop=-10.;
        for(int32 I=1;I<OutRoute.Num();++I)
        {
            auto& Previous=OutRoute[I-1];auto& Current=OutRoute[I];
            // Trigger on the waypoint entered, as WORKER.Move does. Prevent
            // restarting the same .8-second jump every occupied cell.
            if(Blocked[IndexOf(Current.Position)]&&Previous.AtSeconds-LastHop>=.8)
            {Previous.bHop=true;LastHop=Previous.AtSeconds;}
            const double HopAge=Previous.AtSeconds-LastHop;
            const double Multiplier=HopAge>=0.&&HopAge<.4?3.:(HopAge<.8?2.:1.);
            Distance+=ScreenDistance(Current.Position-Previous.Position)/Multiplier;
            Current.AtSeconds=TimeAtDistance(Distance);
        }
        OutDuration=OutRoute.Last().AtSeconds;
        return true;
    }

    FSample SampleRoute(const TArray<FRoutePoint>& Route,double Elapsed)
    {
        FSample Result;if(Route.IsEmpty())return Result;
        if(!FMath::IsFinite(Elapsed))Elapsed=0.;Elapsed=FMath::Max(0.,Elapsed);
        Result.Position=Route[0].Position;
        if(!Finite(Result.Position))return FSample();
        double HopStart=-10.;
        for(int32 I=0;I<Route.Num();++I)
        {
            const auto& P=Route[I];
            if(!Finite(P.Position)||!FMath::IsFinite(P.AtSeconds)||P.AtSeconds<0.)return Result;
            if(P.bHop&&P.AtSeconds<=Elapsed)HopStart=P.AtSeconds;
            if(I+1>=Route.Num())
            {Result.Position=P.Position;Result.bArrived=true;break;}
            const auto& Next=Route[I+1];
            if(!Finite(Next.Position)||!FMath::IsFinite(Next.AtSeconds)||Next.AtSeconds<P.AtSeconds)return Result;
            const FVector2D Direction=Next.Position-P.Position;
            if(!Direction.IsNearlyZero())Result.Facing=Direction.GetSafeNormal();
            if(Elapsed<Next.AtSeconds)
            {
                const double Alpha=FMath::Clamp((Elapsed-P.AtSeconds)/FMath::Max(.000001,Next.AtSeconds-P.AtSeconds),0.,1.);
                Result.Position=P.Position+Direction*Alpha;break;
            }
        }
        const double Age=Elapsed-HopStart;
        if(Age>=0.&&Age<.4)Result.HopHeightPixels=40.*FMath::Sin(Age/.4*PI*.5);
        else if(Age<.8)Result.HopHeightPixels=40.*(1.-BounceOut((Age-.4)/.4));
        return Result;
    }
}
