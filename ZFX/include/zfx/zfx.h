#pragma once

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <tuple>
#include <map>

namespace zfx {

struct Options {
    std::map<std::string, int> symdims;
    std::map<std::string, int> pardims;
    int arch_nregs = 8;

    void define_symbol(std::string const &name, int dimension) {
        symdims[name] = dimension;
    }

    void define_param(std::string const &name, int dimension) {
        pardims[name] = dimension;
    }

    void dump(std::ostream &os) const {
        for (auto const &[name, dim]: symdims) {
            os << '/' << name << '/' << dim;
        }
        for (auto const &[name, dim]: pardims) {
            os << '\\' << name << '\\' << dim;
        }
        os << '|' << arch_nregs;
    }
};

std::tuple
    < std::string
    , std::vector<std::pair<std::string, int>>
    , std::vector<std::pair<std::string, int>>
    > compile_to_assembly
    ( std::string const &code
    , Options const &options
    );

struct Program {
    std::vector<std::pair<std::string, int>> symbols;
    std::vector<std::pair<std::string, int>> params;
    std::string assembly;

    auto const &get_symbols() const {
        return symbols;
    }

    auto const &get_params() const {
        return params;
    }

    int symbol_id(std::string const &name, int dim) const {
        auto it = std::find(
            symbols.begin(), symbols.end(), std::pair{name, dim});
        return it != symbols.end() ? it - symbols.begin() : -1;
    }

    int param_id(std::string const &name, int dim) const {
        auto it = std::find(
            params.begin(), params.end(), std::pair{name, dim});
        return it != params.end() ? it - params.begin() : -1;
    }
};

struct Compiler {
    std::map<std::string, std::unique_ptr<Program>> cache;

    Program *compile
        ( std::string const &code
        , Options const &options
        ) {
        std::ostringstream ss;
        ss << code << "<EOF>";
        options.dump(ss);
        auto key = ss.str();

        auto it = cache.find(key);
        if (it != cache.end()) {
            return it->second.get();
        }

        auto 
            [ assembly
            , symbols
            , params
            ] = compile_to_assembly
            ( code
            , options
            );
        auto prog = std::make_unique<Program>();
        prog->assembly = assembly;
        prog->symbols = symbols;
        prog->params = params;

        auto raw_ptr = prog.get();
        cache[key] = std::move(prog);
        return raw_ptr;
    }
};

}
