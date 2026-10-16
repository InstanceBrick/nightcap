#include "mocktail/vr/performance_telemetry.h"
#include <gtest/gtest.h>
#include <sys/wait.h>
#include <string>

namespace mocktail::vr {
namespace {
constexpr std::uint64_t kStart = 10'000'000'000ULL;
PerformanceSnapshot Frame(std::uint64_t time, std::uint64_t count) {
  return {time, count, time, 123, 2160, 2160, true, 1728, 1728};
}

TEST(PerformanceRate, ReportsApplicationFramesRatherThanRefreshCap) {
  PerformanceRate rate;
  EXPECT_LT(rate.Sample(kStart, Frame(kStart, 100)), 0);
  EXPECT_DOUBLE_EQ(rate.Sample(kStart+1'000'000'000, Frame(kStart+1'000'000'000, 172)), 72.);
}
TEST(PerformanceRate, Reports96OnlyFor96NewFrames) {
  PerformanceRate rate;
  rate.Sample(kStart, Frame(kStart, 0));
  EXPECT_DOUBLE_EQ(rate.Sample(kStart+1'000'000'000, Frame(kStart+1'000'000'000, 96)), 96.);
}
TEST(PerformanceRate, RollsForwardInsteadOfAveragingWholeSession) {
  PerformanceRate rate;
  for (int i=0; i<=4; ++i) rate.Sample(kStart+i*250'000'000ULL, Frame(kStart+i*250'000'000ULL, i*24));
  double fps=0;
  for (int i=1; i<=4; ++i) fps=rate.Sample(kStart+1'000'000'000+i*250'000'000ULL,
      Frame(kStart+1'000'000'000+i*250'000'000ULL, 96+i*12));
  EXPECT_DOUBLE_EQ(fps,48.);
}
TEST(PerformanceRate, WaitsForHalfSecondOfEvidence) {
  PerformanceRate rate;
  rate.Sample(kStart, Frame(kStart, 0));
  EXPECT_LT(rate.Sample(kStart+250'000'000, Frame(kStart+250'000'000,24)),0);
  EXPECT_DOUBLE_EQ(rate.Sample(kStart+500'000'000, Frame(kStart+500'000'000,48)),96.);
}
TEST(PerformanceRate, NoNewFramesIsZeroEvenIfCompositorKeepsPresenting) {
  PerformanceRate rate;
  rate.Sample(kStart, Frame(kStart, 100));
  auto frame=Frame(kStart+1'000'000'000, 100);
  frame.last_frame_ns=kStart;
  EXPECT_DOUBLE_EQ(rate.Sample(frame.timestamp_ns, frame),0.);
}
TEST(PerformanceRate, InvisibleSessionClearsHistory) {
  PerformanceRate rate;
  rate.Sample(kStart,Frame(kStart,100));
  auto frame=Frame(kStart+500'000'000,148);frame.visible=false;
  EXPECT_LT(rate.Sample(frame.timestamp_ns,frame),0);
  EXPECT_LT(rate.Sample(kStart+1'000'000'000,Frame(kStart+1'000'000'000,196)),0);
}
TEST(PerformanceRate, StaleAndFutureTelemetryDoNotShowOldFps) {
  PerformanceRate rate;
  EXPECT_LT(rate.Sample(kStart+2'000'000'000,Frame(kStart,100)),0);
  EXPECT_LT(rate.Sample(kStart,Frame(kStart+1,100)),0);
}
TEST(PerformanceRate, CounterRestartAndClockResetClearHistory) {
  PerformanceRate rate;
  rate.Sample(kStart,Frame(kStart,1000));
  EXPECT_LT(rate.Sample(kStart+500'000'000,Frame(kStart+500'000'000,1)),0);
  rate.Sample(kStart+1'000'000'000,Frame(kStart+1'000'000'000,49));
  EXPECT_LT(rate.Sample(kStart,Frame(kStart,50)),0);
}

class PerformanceMappingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    char pattern[]="/tmp/nightcap-performance-test-XXXXXX";
    fd_=mkstemp(pattern); ASSERT_GE(fd_,0); path_=pattern;
    ASSERT_EQ(ftruncate(fd_,4096),0);
  }
  void TearDown() override { close(fd_);unlink(path_.c_str()); }
  int fd_=-1;std::string path_;
};
TEST_F(PerformanceMappingTest, ReaderWaitsForPublisherAndReadsAllFields) {
  PerformanceMapping writer,reader;
  ASSERT_TRUE(reader.Open(path_.c_str(),false));
  PerformanceSnapshot read;
  EXPECT_FALSE(reader.Read(&read));
  ASSERT_TRUE(writer.Open(path_.c_str(),true));
  writer.Publish(Frame(kStart,72));
  ASSERT_TRUE(reader.Read(&read));
  EXPECT_EQ(read.timestamp_ns,kStart);EXPECT_EQ(read.frames,72);
  EXPECT_EQ(read.process,123);EXPECT_EQ(read.width,2160);EXPECT_TRUE(read.visible);
  EXPECT_EQ(read.render_width,1728);EXPECT_EQ(read.render_height,1728);
}
TEST_F(PerformanceMappingTest, ReadsConsistentSnapshotsAcrossProcesses) {
  PerformanceMapping writer,reader;
  ASSERT_TRUE(writer.Open(path_.c_str(),true));
  ASSERT_TRUE(reader.Open(path_.c_str(),false));
  writer.Publish(Frame(kStart,1));
  const auto child=fork();ASSERT_GE(child,0);
  if(child==0) {
    for(std::uint64_t i=2;i<100000;++i) {
      auto frame=Frame(kStart+i,i);frame.width=i;frame.height=i;
      writer.Publish(frame);
    }
    _exit(0);
  }
  for(int i=0;i<100000;++i) {
    PerformanceSnapshot frame;
    if(reader.Read(&frame) && frame.frames>1) {
      EXPECT_EQ(frame.width,frame.frames);EXPECT_EQ(frame.height,frame.frames);
      EXPECT_EQ(frame.timestamp_ns,kStart+frame.frames);
    }
  }
  int status=0;ASSERT_EQ(waitpid(child,&status,0),child);EXPECT_EQ(status,0);
}
TEST_F(PerformanceMappingTest, RejectsPublicShortAndSymlinkFiles) {
  PerformanceMapping mapping;
  ASSERT_EQ(fchmod(fd_,0644),0);EXPECT_FALSE(mapping.Open(path_.c_str(),false));
  ASSERT_EQ(fchmod(fd_,0600),0);ASSERT_EQ(ftruncate(fd_,1),0);
  EXPECT_FALSE(mapping.Open(path_.c_str(),false));
  ASSERT_EQ(ftruncate(fd_,4096),0);
  const auto link=path_+"-link";ASSERT_EQ(symlink(path_.c_str(),link.c_str()),0);
  EXPECT_FALSE(mapping.Open(link.c_str(),true));unlink(link.c_str());
}
}  // namespace
}  // namespace mocktail::vr
