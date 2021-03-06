/*
  Copyright (c) 2015, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "mysql/harness/config_parser.h"
#include "mysql/harness/filesystem.h"
#include "mysql/harness/plugin.h"

////////////////////////////////////////
// Test system include files
#include "test/helpers.h"

////////////////////////////////////////
// Third-party include files
#include "gmock/gmock.h"
#include "gtest/gtest.h"

////////////////////////////////////////
// Standard include files
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using mysql_harness::Config;
using mysql_harness::ConfigSection;
using mysql_harness::Path;
using mysql_harness::bad_option;
using mysql_harness::bad_section;
using mysql_harness::syntax_error;

using testing::ElementsAreArray;
using testing::IsEmpty;
using testing::SizeIs;
using testing::UnorderedElementsAreArray;
using testing::TestWithParam;
using testing::ValuesIn;
using testing::Eq;

namespace mysql_harness {

bool operator==(const Config& lhs, const Config& rhs) {
  // We just check the section names to start with
  auto&& lhs_names = lhs.section_names();
  auto&& rhs_names = rhs.section_names();

  // Check if the sizes differ. This is not an optimization since
  // std::equal does not work properly on ranges of unequal size.
  if (lhs_names.size() != rhs_names.size())
    return false;

  // Put the lists in vectors and sort them
  std::vector<std::pair<std::string, std::string>>
    lhs_vec(lhs_names.begin(), lhs_names.end());
  std::sort(lhs_vec.begin(), lhs_vec.end());

  std::vector<std::pair<std::string, std::string>>
    rhs_vec(rhs_names.begin(), rhs_names.end());
  std::sort(rhs_vec.begin(), rhs_vec.end());

  // Compare the elements of the sorted vectors
  return std::equal(lhs_vec.begin(), lhs_vec.end(), rhs_vec.begin());
}

}

std::list<std::string>
section_names(const mysql_harness::Config::ConstSectionList& sections) {
  std::list<std::string> result;
  for (auto& section : sections)
    result.push_back(section->name);
  std::cerr << result << std::endl;
  return result;
}


void PrintTo(const Config& config, std::ostream& out) {
  for (auto&& val : config.section_names())
    out << val.first << ":" << val.second << " ";
}

class ConfigTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    std::vector<std::string> words;
    words.push_back("reserved");
    config.set_reserved(words);
  }

  Config config;
};

Path g_here;

TEST_F(ConfigTest, TestEmpty) {
  EXPECT_TRUE(config.is_reserved("reserved"));
  EXPECT_FALSE(config.is_reserved("legal"));

  // A newly created configuration is always empty.
  EXPECT_TRUE(config.empty());

  // Test that fetching a non-existing section throws an exception.
  EXPECT_THROW(config.get("magic"), std::runtime_error);

  EXPECT_FALSE(config.has("magic"));
}

TEST_F(ConfigTest, SetGetTest) {
  // Add the section
  config.add("magic");

  // Test that fetching a section get the right section back.
  EXPECT_TRUE(config.has("magic"));

  Config::SectionList sections = config.get("magic");
  EXPECT_EQ(1U, sections.size());

  ConfigSection* section = sections.front();
  EXPECT_EQ("magic", section->name);

  // Test that fetching a non-existing option in a section throws a
  // run-time exception.
  EXPECT_THROW(section->get("my_option"), std::runtime_error);

  // Set the value of the option in the section
  section->set("my_option", "my_value");

  // Check that the value can be retrieved.
  EXPECT_EQ("my_value", section->get("my_option"));

  config.clear();
  EXPECT_TRUE(config.empty());
}


class GoodParseTestAllowKey : public ::testing::TestWithParam<const char*> {
 protected:
  virtual void SetUp() {
    config = new Config(Config::allow_keys);

    std::vector<std::string> words;
    words.push_back("reserved");
    config->set_reserved(words);

    std::istringstream input(GetParam());
    config->read(input);
  }

  virtual void TearDown() {
    delete config;
    config = nullptr;
  }

  Config *config;
};

TEST_P(GoodParseTestAllowKey, SectionOne) {
  // Checking that getting a non-existent section throws exception
  EXPECT_THROW(config->get("nonexistant-section"), bad_section);

  Config::SectionList sections = config->get("one");
  EXPECT_EQ(1U, sections.size());

  ConfigSection* section = sections.front();
  EXPECT_EQ("one", section->name);
  EXPECT_EQ("bar", section->get("foo"));

  // Checking that getting a non-existient option in an existing
  // section throws exception.
  EXPECT_THROW(section->get("nonexistant-option"), bad_option);
}

const char *good_examples[] = {
  ("[one]\n" "foo = bar\n"),
  ("[one]\n" "foo: bar\n"),
  (" [one]   \n" "  foo: bar   \n"),
  (" [one]\n" "  foo   :bar   \n"),
  ("# Hello\n"
   " [one]\n" "  foo   :bar   \n"),
  ("# Hello\n"
   "# World!\n"
   " [one]\n" "  foo   :bar   \n"),
  ("; Hello\n"
   " [one]\n" "  foo   :bar   \n"),
  ("[DEFAULT]\n" "foo = bar\n"
   "[one]\n"),
  ("[DEFAULT]\n" "other = ar\n"
   "[one]\n" "foo = b{other}\n"),
  ("[DEFAULT]\n" "one = b\n" "two = r\n"
   "[one]\n" "foo = {one}a{two}\n"),
  ("[DEFAULT]\n" "one = b\n" "two = r\n"
   "[one:my_key]\n" "foo = {one}a{two}\n")
};

INSTANTIATE_TEST_CASE_P(TestParsing, GoodParseTestAllowKey,
                        ::testing::ValuesIn(good_examples));

// Test fixture to compare option value with the result of
// interpolating the value.
using Sample = std::pair<std::string, std::string>;
class TestInterpolate : public TestWithParam<Sample> {
 protected:
  virtual void SetUp() {
    config_ = new Config(Config::allow_keys);
    config_->add("testing", "a_key");
    config_->set_default("datadir", "--path--");
  }

  virtual void TearDown() {
    delete config_;
    config_ = nullptr;
  }

  Config *config_;
};

TEST_P(TestInterpolate, CheckExpected) {
  auto value = std::get<0>(GetParam());
  auto expect = std::get<1>(GetParam());

  auto&& section = config_->get("testing", "a_key");
  section.set("option_name", value);
  EXPECT_THAT(section.get("option_name"), Eq(expect));
}

Sample interpolate_examples[] = {
  {"foo",                       "foo"},
  {"c:\\foo\\bar\\{datadir}",   "c:\\foo\\bar\\--path--"},
  {"c:\\foo\\bar\\{undefined}", "c:\\foo\\bar\\{undefined}"},
  {"{datadir}\\foo",            "--path--\\foo"},
  {"{datadir}",                 "--path--"},
  {"foo{datadir}bar",           "foo--path--bar"},
  {"{{datadir}}",               "{--path--}"},
  {"{datadir}}",                "--path--}"},
  {"{{datadir}",                "{--path--"},
  {"{{{datadir}}}",             "{{--path--}}"},
  {"{datadir",                  "{datadir"},
  {"c:\\foo\\bar\\{425432-5425432-5423534253-542342}",
   "c:\\foo\\bar\\{425432-5425432-5423534253-542342}"},
};

INSTANTIATE_TEST_CASE_P(TestParsing, TestInterpolate,
                        ValuesIn(interpolate_examples));

TEST(TestConfig, RecursiveInterpolate) {
  const char *const config_text{
    "[DEFAULT]\n"
    "basedir = /root/dir\n"
    "datadir = {basedir}/data\n"

    "[one]\n"
    "log = {datadir}/router.log\n"
    "rec = {other}\n"  // Recursive reference
    "other = {rec}\n"
  };

  Config config(Config::allow_keys);
  std::istringstream input(config_text);
  config.read(input);

  auto&& section = config.get("one", "");
  EXPECT_THAT(section.get("log"), Eq("/root/dir/data/router.log"));
  EXPECT_THROW(section.get("rec"), syntax_error);
}

class BadParseTestForbidKey : public ::testing::TestWithParam<const char*> {
 protected:
  virtual void SetUp() {
    config = new Config;

    std::vector<std::string> words;
    words.push_back("reserved");
    config->set_reserved(words);
  }

  virtual void TearDown() {
    delete config;
    config = nullptr;
  }

  Config *config;
};

TEST_P(BadParseTestForbidKey, SyntaxError) {
  std::istringstream input{GetParam()};
  EXPECT_ANY_THROW(config->read(input));
}

static const char* syntax_problems[] = {
  // Unterminated section header line
  ("[one\n" "foo = bar\n"),

  // Malformed start of a section
  ("one]\n" "foo: bar\n"),

  // Bad section name
  ("[one]\n" "foo = bar\n"
   "[reserved]\n" "foo = baz\n"),

  // Options before first section
  ("  foo: bar   \n" "[one]\n"),

  // Unterminated last line
  ("[one]\n" "foo = bar"),

  // Repeated option
  ("[one]\n" "foo = bar\n" "foo = baz\n"),
  ("[one]\n" "foo = bar\n" "Foo = baz\n"),

  // Space in option
  ("[one]\n" "foo bar = bar\n" "bar = baz\n"),

  // Repeated section
  ("[one]\n" "foo = bar\n" "[one]\n" "foo = baz\n"),
  ("[one]\n" "foo = bar\n" "[ONE]\n" "foo = baz\n"),

  // Key but keys not allowed
  ("[one:my_key]\n" "foo = bar\n" "[two]\n" "foo = baz\n"),
};

INSTANTIATE_TEST_CASE_P(TestParsingSyntaxError, BadParseTestForbidKey,
                        ::testing::ValuesIn(syntax_problems));

class BadParseTestAllowKeys : public ::testing::TestWithParam<const char*> {
 protected:
  virtual void SetUp() {
    config = new Config(Config::allow_keys);

    std::vector<std::string> words;
    words.push_back("reserved");
    config->set_reserved(words);
  }

  virtual void TearDown() {
    delete config;
    config = nullptr;
  }

  Config *config;
};

TEST_P(BadParseTestAllowKeys, SemanticError) {
  std::istringstream input{GetParam()};
  EXPECT_THROW(config->read(input), syntax_error);
}

static const char* semantic_problems[] = {
  // Empty key
  ("[one:]\n" "foo = bar\n" "[two]\n" "foo = baz\n"),

  // Key on default section
  ("[DEFAULT:key]\n" "one = b\n" "two = r\n"
   "[one:key1]\n" "foo = {one}a{two}\n"
   "[one:key2]\n" "foo = {one}a{two}\n"),
};

INSTANTIATE_TEST_CASE_P(TestParseErrorAllowKeys, BadParseTestAllowKeys,
                        ::testing::ValuesIn(semantic_problems));

TEST(TestConfig, ConfigUpdate) {
  const char *const configs[]{
    ("[one]\n"
     "one = first\n"
     "two = second\n"),
    ("[one]\n"
     "one = new first\n"
     "[two]\n"
     "one = first\n"),
  };

  Config config(Config::allow_keys);
  std::istringstream input(configs[0]);
  config.read(input);

  Config other(Config::allow_keys);
  std::istringstream other_input(configs[1]);
  other.read(other_input);

  Config expected(Config::allow_keys);
  config.update(other);

  ConfigSection& one = config.get("one", "");
  ConfigSection& two = config.get("two", "");
  EXPECT_EQ("new first", one.get("one"));
  EXPECT_EQ("second", one.get("two"));
  EXPECT_EQ("first", two.get("one"));

  // Non-existent options should still throw an exception
  auto&& section = config.get("one", "");
  EXPECT_THROW(section.get("nonexistant-option"), bad_option);

  // Check that merging sections with mismatching names generates an
  // exception
  EXPECT_THROW(one.update(two), bad_section);
}

TEST(TestConfig, ConfigReadBasic) {
  // Here are three different sources of configurations that should
  // all be identical. One is a single file, one is a directory, and
  // one is a stream.

  Config dir_config = Config(Config::allow_keys);
  dir_config.read(g_here.join("data/logger.d"), "*.cfg");

  Config file_config = Config(Config::allow_keys);
  file_config.read(g_here.join("data/logger.cfg"));

  const char *const config_string =
    ("[DEFAULT]\n"
     "logging_folder = var/log\n"
     "config_folder = etc\n"
     "plugin_folder = var/lib\n"
     "runtime_folder = var/run\n"
     "[logger]\n"
     "library = logger\n"
     "[example]\n"
     "library = example\n"
     "[magic]\n"
     "library = magic\n"
     "message = Some kind of\n");

  Config stream_config(Config::allow_keys);
  std::istringstream stream_input(config_string);
  stream_config.read(stream_input);

  EXPECT_EQ(dir_config, file_config);
  EXPECT_EQ(dir_config, stream_config);
  EXPECT_EQ(file_config, stream_config);
}


// Here we test that reads of configuration entries overwrite previous
// read entries.
TEST(TestConfig, ConfigReadOverwrite) {
  Config config = Config(Config::allow_keys);
  config.read(g_here.join("data/logger.d"), "*.cfg");
  EXPECT_EQ("Some kind of", config.get("magic", "").get("message"));

  // Non-existent options should still throw an exception
  {
    auto&& section = config.get("magic", "");
    EXPECT_THROW(section.get("not-in-section"), bad_option);
  }

  config.read(g_here.join("data/magic-alt.cfg"));
  EXPECT_EQ("Another message", config.get("magic", "").get("message"));

  // Non-existent options should still throw an exception
  {
    auto&& section = config.get("magic", "");
    EXPECT_THROW(section.get("not-in-section"), bad_option);
  }
}

TEST(TestConfig, SectionRead) {
  static const char *const config_string =
    ("[DEFAULT]\n"
     "logging_folder = var/log\n"
     "config_folder = etc\n"
     "plugin_folder = var/lib\n"
     "runtime_folder = var/run\n"
     "[logger]\n"
     "library = logger\n"
     "[empty]\n"
     "[example]\n"
     "library = magic\n"
     "message = Some kind of\n");

  Config config(Config::allow_keys);
  std::istringstream stream_input(config_string);
  config.read(stream_input);

  // Test that the sections command return the right sections
  std::string expected[] = {"logger", "example", "empty"};
  EXPECT_THAT(section_names(config.sections()),
              UnorderedElementsAreArray(expected));

  // Test that options for a section is correct
  std::vector<std::pair<std::string, std::string>> expected_options{
    {"library", "magic"},
    {"message", "Some kind of"}
  };
  EXPECT_THAT(config.get("example", "").get_options(),
              ElementsAreArray(expected_options));
  EXPECT_THAT(config.get("example", "").get_options(),
              SizeIs(2));
  EXPECT_THAT(config.get("empty", "").get_options(),
              IsEmpty());
  EXPECT_THAT(config.get("empty", "").get_options(),
              SizeIs(0));
}

int main(int argc, char *argv[]) {
  g_here = Path(argv[0]).dirname();

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
