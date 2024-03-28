# DuckPG - DuckDB extension with postgresql wire protocol enabled

## Changelog for this fork
Compared to the version of the forked one,

* `std::format` is not supported until C++23, so currently, `std::ostringstream` is used as a workaround.
* In `/src/duckpg/duckdb_pgwire_extension.cpp`, the line 125 is changed to
```c++
  stmt.fields.emplace_back(pgwire::FieldDescription{name, oid});
```

  Theory related to this:
```c++
struct S {
  int a;
  int b;
}

int main() {
  S s1{2, 3}; // aggregate initialization
  S s2; // default initialization with a and b set to 0;
  // when you call it like this
  std::vector<S> ss;
  ss.emplace_back(2, 3); // This fails as there isn't an allocator for S that expects two int initializers (aggregate initialization is just a convenient syntax & semantic)
  ss.emplace_back(); // This works though because the allocator picked for S is the compiler's default constructor
  ss.emplace_back(S{2,3}); // This works though because the allocator picked for S is the compiler's default copy constructor
}
```

The FieldDescription struct in the repo does not work because it runs into the same situation.

Adding an explicit constructor would work:

```c++
struct S {
  int a;
  int b;

  S(int a, int b): a { a }, b { b } {}
}

int main() {
  // when you call it like this
  std::vector<S> ss;
  ss.emplace_back(2, 3); // This now succeeds because there's an allocator for two int initializer
}
```

Disclaimer: just my theory, I haven't dug into how Allocator in C++ works though

## Introduction
This extension is used for experimentation of adding PostgreSQL wire protocol to the DuckDB ecosystem through an extension named `duckdb_pgwire`.

The PostgreSQL wire protocol is implemented in standalone component/library named `pgwire` that heavily inspired by https://github.com/returnString/convergence and https://github.com/jeroenrinzema/psql-wire.

## Milestone

- [x] Simple query support
- [x] DuckDB extension
- [x] Simple golang client
- [ ] PGWire unit tests
- [ ] Support more data type
- [ ] Logging
- [ ] Configuration
- [ ] Session Manager
- [ ] Extended Query
- [ ] So on...

## Building and Running

Clone the repository along with the submodule with :
```bash
git clone --recurse-submodules https://github.com/Huy-DNA/duckpg.git
```

Build the extension
```bash
make -j$(nproc)
```

Open the duckdb shell or through any duckdb embedded client and load the extension
```bash
# example usage with cli
cd build/release
# somehow still require manual loading of the extension even already built onto the duckdb shell/cli
./duckdb -cmd 'load duckdb_pgwire'   

```

And on the other terminal you can use psql and connect to port 15432 with ssl disabled
```bash
psql 'postgresql://localhost:15432/main' -c 'select * from generate_series(0, 100)'
```

Or you can use the postgresql driver in your language choice.
You can also run sample client in golang provided in this repo
```bash
# from the root directory
cd client/go/cmd/simple
go build && ./simple
```
