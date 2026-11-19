#include "../Source/RunwayProject.h"
#include <cstdlib>
#include <iostream>

#define CHECK(expr) do { if (!(expr)) { std::cerr << __LINE__ << ": " #expr "\n"; return EXIT_FAILURE; } } while(false)

int main()
{
    licklubin::RunwayProject p(41);
    const auto a=p.createLoop();
    CHECK(a==1 && p.loopName(a).size()==12 && !p.writtenRange());
    CHECK(p.setStep(a, 1, 1, 1, {7,11,14}));
    CHECK(p.setStep(a, 1, 1, 11, {7},19));
    CHECK(!p.setStep(a, 1, 1, 11, {7,11},19));
    CHECK(!p.transpose(a, 1, 1, 11, 6));
    CHECK(p.event(a,1,1,11)->slideTarget==19);
    const auto b=p.duplicateLoop(a);
    CHECK(b==2 && p.loopName(b)!=p.loopName(a));
    CHECK(p.transpose(b,1,1,11,-2) && p.event(b,1,1,11)->slideTarget==17);
    CHECK(p.event(a,1,1,11)->pitches.front()==7);
    CHECK(p.setStep(b,1,1,1,{}) && !p.event(b,1,1,1) && p.event(a,1,1,1)->pitches.size()==3);
    CHECK(!p.setStep(a,17,1,1,{1}) && !p.setStep(a,1,1,0,{1}));
    CHECK(p.placeRange(a,{17,32}) && !p.placeRange(a,{2,17}));
    CHECK(p.writtenRange()->first==17 && p.writtenRange()->last==32);
    CHECK(!p.placeSections(b,{1,2}) && !p.resolve(1));
    CHECK(p.placeSections(a,{1,2,3,4}));
    CHECK(p.replaceSections(a,b,{2,4}));
    CHECK(!p.replaceSections(a,b,{1,2}) && p.resolve(1)->loopId==a);
    CHECK(p.resolve(17)->loopId==b && p.resolve(17)->loopBar==1);
    CHECK(p.resolve(64)->loopId==b && p.resolve(64)->loopBar==16 && !p.resolve(65));
    CHECK(p.groupForBar(4)==1 && p.groupForBar(5)==2 && p.groupForBar(64)==16);
    std::cout << "Runway project scenarios passed\n";
}
