/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/traced/probes/ftrace/vendor_tracepoints.h"

#include <vector>

#include "test/gtest_and_gmock.h"

#include "src/base/test/tmp_dir_tree.h"
#include "src/traced/probes/ftrace/atrace_hal_wrapper.h"
#include "src/traced/probes/ftrace/ftrace_procfs.h"

using testing::_;
using testing::AnyNumber;
using testing::ElementsAre;
using testing::HasSubstr;
using testing::NiceMock;
using testing::Pair;
using testing::Return;
using testing::Sequence;

namespace perfetto {
namespace vendor_tracepoints {
namespace {

class MockHal : public AtraceHalWrapper {
 public:
  MockHal() : AtraceHalWrapper() {}
  MOCK_METHOD0(ListCategories, std::vector<std::string>());
  MOCK_METHOD1(EnableCategories, bool(const std::vector<std::string>&));
  MOCK_METHOD0(DisableAllCategories, bool());
};

class MockFtraceProcfs : public FtraceProcfs {
 public:
  MockFtraceProcfs() : FtraceProcfs("/root/") {
    ON_CALL(*this, NumberOfCpus()).WillByDefault(Return(1));
    ON_CALL(*this, WriteToFile(_, _)).WillByDefault(Return(true));
    ON_CALL(*this, ClearFile(_)).WillByDefault(Return(true));
    EXPECT_CALL(*this, NumberOfCpus()).Times(AnyNumber());
  }

  MOCK_METHOD2(WriteToFile,
               bool(const std::string& path, const std::string& str));
  MOCK_METHOD2(AppendToFile,
               bool(const std::string& path, const std::string& str));
  MOCK_METHOD1(ReadOneCharFromFile, char(const std::string& path));
  MOCK_METHOD1(ClearFile, bool(const std::string& path));
  MOCK_CONST_METHOD1(ReadFileIntoString, std::string(const std::string& path));
  MOCK_METHOD0(ReadEnabledEvents, std::vector<std::string>());
  MOCK_CONST_METHOD0(NumberOfCpus, size_t());
  MOCK_CONST_METHOD1(GetEventNamesForGroup,
                     const std::set<std::string>(const std::string& path));
};

TEST(DiscoverVendorTracepointsTest, DiscoverVendorTracepointsWithHal) {
  MockHal hal;
  MockFtraceProcfs ftrace;
  Sequence s;

  EXPECT_CALL(hal, ListCategories())
      .InSequence(s)
      .WillOnce(Return(std::vector<std::string>({"gfx"})));
  EXPECT_CALL(ftrace, WriteToFile("/root/events/enable", "0"))
      .InSequence(s)
      .WillOnce(Return(true));
  EXPECT_CALL(hal, EnableCategories(ElementsAre("gfx")))
      .InSequence(s)
      .WillOnce(Return(true));
  EXPECT_CALL(ftrace, ReadEnabledEvents())
      .InSequence(s)
      .WillOnce(Return(std::vector<std::string>({"foo/bar", "a/b"})));
  EXPECT_CALL(hal, DisableAllCategories()).InSequence(s).WillOnce(Return(true));
  EXPECT_CALL(ftrace, WriteToFile("/root/events/enable", "0"))
      .InSequence(s)
      .WillOnce(Return(true));

  EXPECT_THAT(DiscoverVendorTracepointsWithHal(&hal, &ftrace),
              ElementsAre(Pair("gfx", ElementsAre(GroupAndName("foo", "bar"),
                                                  GroupAndName("a", "b")))));
}

TEST(DiscoverVendorTracepointsTest, DiscoverVendorTracepointsWithFileOk) {
  base::TmpDirTree tree;
  std::string contents =
      "gfx\n"
      " foo/bar\n"
      " g/a\n"
      " g/b\n"
      "memory\n"
      " grp/evt\n";
  tree.AddFile("vendor_atrace.txt", contents);

  std::map<std::string, std::vector<GroupAndName>> result;
  base::Status status = DiscoverVendorTracepointsWithFile(
      tree.AbsolutePath("vendor_atrace.txt"), &result);

  ASSERT_TRUE(status.ok()) << status.message();
  EXPECT_THAT(
      result,
      ElementsAre(Pair("gfx", ElementsAre(GroupAndName("foo", "bar"),
                                          GroupAndName("g", "a"),
                                          GroupAndName("g", "b"))),
                  Pair("memory", ElementsAre(GroupAndName("grp", "evt")))));
}

TEST(DiscoverVendorTracepointsTest,
     DiscoverVendorTracepointsWithFileEmptyLines) {
  base::TmpDirTree tree;
  std::string contents =
      "\n"
      "gfx\n"
      "   \n"
      " foo/bar\n"
      "\n";
  tree.AddFile("vendor_atrace.txt", contents);

  std::map<std::string, std::vector<GroupAndName>> result;
  base::Status status = DiscoverVendorTracepointsWithFile(
      tree.AbsolutePath("vendor_atrace.txt"), &result);

  ASSERT_TRUE(status.ok()) << status.message();
  EXPECT_THAT(result, ElementsAre(Pair(
                          "gfx", ElementsAre(GroupAndName("foo", "bar")))));
}

TEST(DiscoverVendorTracepointsTest,
     DiscoverVendorTracepointsWithFileWhitespaces) {
  base::TmpDirTree tree;
  std::string contents =
      "gfx\n"
      " path/1\n"
      "\tpath/2\n"
      "  path/3\n"
      "\t\tpath/4\n";
  tree.AddFile("vendor_atrace.txt", contents);

  std::map<std::string, std::vector<GroupAndName>> result;
  base::Status status = DiscoverVendorTracepointsWithFile(
      tree.AbsolutePath("vendor_atrace.txt"), &result);

  ASSERT_TRUE(status.ok()) << status.message();
  EXPECT_THAT(result,
              ElementsAre(Pair("gfx", ElementsAre(GroupAndName("path", "1"),
                                                  GroupAndName("path", "2"),
                                                  GroupAndName("path", "3"),
                                                  GroupAndName("path", "4")))));
}

TEST(DiscoverVendorTracepointsTest,
     DiscoverVendorTracepointsWithFileNoCategory) {
  base::TmpDirTree tree;
  std::string contents =
      " foo/bar\n"
      " g/a\n"
      " g/b\n";
  tree.AddFile("vendor_atrace.txt", contents);

  std::map<std::string, std::vector<GroupAndName>> result;
  base::Status status = DiscoverVendorTracepointsWithFile(
      tree.AbsolutePath("vendor_atrace.txt"), &result);

  EXPECT_THAT(status.message(), HasSubstr("Ftrace event path before category"));
}

TEST(DiscoverVendorTracepointsTest, DiscoverVendorTracepointsWithFileNoSlash) {
  base::TmpDirTree tree;
  std::string contents =
      "gfx\n"
      " event\n";
  tree.AddFile("vendor_atrace.txt", contents);

  std::map<std::string, std::vector<GroupAndName>> result;
  base::Status status = DiscoverVendorTracepointsWithFile(
      tree.AbsolutePath("vendor_atrace.txt"), &result);

  EXPECT_THAT(status.message(),
              HasSubstr("Ftrace event path not in group/event format"));
}

TEST(DiscoverVendorTracepointsTest,
     DiscoverVendorTracepointsWithFileEmptyGroup) {
  base::TmpDirTree tree;
  std::string contents =
      "gfx\n"
      " /event\n";
  tree.AddFile("vendor_atrace.txt", contents);

  std::map<std::string, std::vector<GroupAndName>> result;
  base::Status status = DiscoverVendorTracepointsWithFile(
      tree.AbsolutePath("vendor_atrace.txt"), &result);

  EXPECT_THAT(status.message(), HasSubstr("group is empty"));
}

TEST(DiscoverVendorTracepointsTest,
     DiscoverVendorTracepointsWithFileTooManySlash) {
  base::TmpDirTree tree;
  std::string contents =
      "gfx\n"
      " group/dir/event\n";
  tree.AddFile("vendor_atrace.txt", contents);

  std::map<std::string, std::vector<GroupAndName>> result;
  base::Status status = DiscoverVendorTracepointsWithFile(
      tree.AbsolutePath("vendor_atrace.txt"), &result);

  EXPECT_THAT(status.message(), HasSubstr("extra /"));
}

TEST(DiscoverVendorTracepointsTest,
     DiscoverVendorTracepointsWithFileNameEmpty) {
  base::TmpDirTree tree;
  std::string contents =
      "gfx\n"
      " group/\n";
  tree.AddFile("vendor_atrace.txt", contents);

  std::map<std::string, std::vector<GroupAndName>> result;
  base::Status status = DiscoverVendorTracepointsWithFile(
      tree.AbsolutePath("vendor_atrace.txt"), &result);

  EXPECT_THAT(status.message(), HasSubstr("name empty"));
}

}  // namespace
}  // namespace vendor_tracepoints
}  // namespace perfetto
