//
// Created by sergo on 09.10.2019.
//

#ifndef STELLAR_CORE_NAME_HPP
#define STELLAR_CORE_NAME_HPP

#include <string>
#include <cassert>
#include <chrono>
#include <random>
#include <iostream>
#include <sstream>

using namespace std;

static const char alphanum[] =
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz";

namespace my {

  const uint DEFAULT_FULL_NAME_LENGTH = 16;
  const uint DEFAULT_NAME_LENGTH = 8;

  class Name;

  static void nameSizeAssertion(string const &name) {
    if (name.size() != DEFAULT_NAME_LENGTH) {
      std::cout << "SIZE ASSERTION IN NAME FAILED" << std::endl;
      std::cout << "NAME: " << name << std::endl;
      throw std::runtime_error("SIZE ASSERTION IN NAME FAILED");
    }
  }

  static void fullNameSizeAssertion(string const &name) {
    if (name.size() != DEFAULT_FULL_NAME_LENGTH) {
      std::cout << "SIZE ASSERTION IN FULL NAME FAILED" << std::endl;
      std::cout << "NAME: " << name << std::endl;
      throw std::runtime_error("SIZE ASSERTION IN FULL NAME FAILED");
    }
  }


  class Name {
  protected:
    static std::string genRandomStr(int len) {
      static unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();

      static std::default_random_engine generator(seed);
      static std::uniform_int_distribution<int> distribution(0, sizeof(alphanum) - 2);

      string result;
      result.resize(len);

      for (int i = 0; i < len; ++i) {
        result[i] = alphanum[distribution(generator)];
      }

      return result;
    }

    static const int MAX_SIZE = 8;
    string _name;
  public:
    Name() : _name(string(genRandomStr(DEFAULT_NAME_LENGTH))) {}

//    Name(Name&& other) = default;
//
    explicit Name(string const &name) {
      nameSizeAssertion(name);
      _name = name;
      _name.resize(8);
    }

    explicit Name(string &&name) {
      nameSizeAssertion(name);
      _name = name;
      _name.resize(8);
    }

    static Name randomName() {
      return Name(genRandomStr(8));
    }

    Name &operator=(Name const &other) = default;

//    Name& operator=(Name&& other) = default;

    bool operator==(Name const &other) const {
      return _name == other._name;
    }

    bool operator!=(Name const &other) const {
      return _name != other._name;
    }

    bool operator<(Name const &other) const {
      return _name < other._name;
    }

    virtual string toString() const {
      return _name;
    }

    virtual ~Name() = default;

    Name operator+(int n) const {
      string str = toString();
      uint64_t sum = 0;
      for (char chr : str) {
        if (chr >= '0' and chr <= '9') {
          sum = sum * 62 + chr - '0';
        } else if (chr >= 'A' and chr <= 'Z') {
          sum = sum * 62 + chr - 'A' + 10;
        } else if (chr >= 'a' and chr <= 'z') {
          sum = sum * 62 + chr - 'a' + 36;
        } else {
          throw std::runtime_error("BAD PEER ADDRESS");
        }
      }
      sum += n;

      int counter = 0;
      while (sum > 0) {
        if (counter >= 8) {
          break;
        }
        uint chr = sum % 62;
        sum /= 62;
        str[7 - counter++] = alphanum[chr];
      }

      return my::Name(str);
    }
  };

  class AppName : public Name {
  public:
    AppName() : Name() {}

//    AppName(AppName const& other) {
//        _name = other._name;
//    }
//    AppName(AppName&& other) = default;
    explicit AppName(string const &str) : Name(str) {}
//    explicit AppName(Name&& other): Name(move(other)) {}

    ~AppName() override = default;
  };

  class PeerName : public Name {
  public:
    PeerName() : Name() {}

    explicit PeerName(Name const &name) : Name(name) {}

    explicit PeerName(stellar::PeerNameXdr const &other) {
      _name = string((char *) &other.data, 8);
    }

    //    PeerName(PeerName const& other) {
//        _name = other._name;
//    }
//    PeerName(PeerName&& other) = default;
//    explicit PeerName(string&& str): Name(move(str)) {
//    }
//    explicit PeerName(Name&& other): Name(move(other)) {}
    PeerName(string const &str) : Name(str) {}

    PeerName operator++(int) {
      int x;
      std::stringstream ss(_name);
      ss >> x;
      ++x;
      int y = x;
      unsigned int number_of_digits = 0;
      do {
        ++number_of_digits;
        y /= 10;
      } while (y);
      std::fill(_name.begin(), _name.begin() + 8 - number_of_digits, '0');
      for (int i = 7; i >= 8 - number_of_digits; --i) {
        _name[i] = std::to_string(x % 10).data()[0];
        x /= 10;
      }
      return *this;
    }

    ~PeerName() override = default;
  };

  class FullName : public AppName, public PeerName {
  public:
    FullName() : AppName(), PeerName() {}

    explicit FullName(string const &name) : AppName(), PeerName() {
      fullNameSizeAssertion(name);
      AppName::_name = name.substr(0, 8);
      PeerName::_name = name.substr(8, 8);
    }

    FullName(string const &appName, string const &peerName) : AppName(appName), PeerName(peerName) {}

    FullName(FullName const &other) : AppName(), PeerName() {
      AppName::_name = other.AppName::_name;
      PeerName::_name = other.PeerName::_name;
    }

    FullName(AppName const &appName, PeerName const &peerName) : AppName(appName), PeerName(peerName) {}

//    FullName(FullName&& other) = default;

//    explicit FullName(stellar::PeerNameXdr const& other) {
//        AppName::_name = string((char*)&other.first, 8);
//        PeerName::_name = string((char*)&other.second, 8);
//    }

    explicit FullName(AppName const &appName) : AppName(appName), PeerName() {}

    static FullName randomName() {
      return FullName(genRandomStr(16));
    }

//    FullName& operator=(FullName const& other) = default;
//
//    FullName& operator=(FullName&& other) = default;

    bool operator==(FullName const &other) const {
      return AppName::_name == other.AppName::_name &&
             PeerName::_name == other.PeerName::_name;
    }

    bool operator!=(FullName const &other) const {
      return AppName::_name != other.AppName::_name ||
             PeerName::_name != other.PeerName::_name;
    }

    bool operator<(FullName const &other) const {
      return
          AppName::_name != other.AppName::_name ?
          AppName::_name<other.AppName::_name :
          PeerName::_name<other.PeerName::_name;
    }

    string const &first() const {
      return AppName::_name;
    }

    string const &second() const {
      return PeerName::_name;
    }

    string toString() const override {
      return first() + second();
    }

    ~FullName() override = default;
  };

}

#endif //STELLAR_CORE_NAME_HPP
