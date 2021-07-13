#pragma once

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <tuple>
#include <map>

namespace zfx {

std::tuple
    < std::string
    , std::vector<std::pair<std::string, int>>
    > compile_to_assembly
    ( std::string const &code
    , std::map<std::string, int> const &symdims
    );

template <class Prog>
struct Program {
    std::unique_ptr<Prog> prog;
    std::vector<std::pair<std::string, int>> symbols;

    int channel_id(std::string const &name, int dim) const {
        auto it = std::find(symbols.begin(), symbols.end(), std::pair{name, dim});
        return it - symbols.begin();
    }

    decltype(auto) make_context() {
        return prog->make_context();
    }
};

template <class Prog>
struct Compiler {
    std::map<std::string, std::unique_ptr<Program<Prog>>> cache;

    Program<Prog> *compile
        ( std::string const &code
        , std::map<std::string, int> const &symdims
        ) {
        std::stringstream ss;
        ss << code << "<EOF>";
        for (auto const &[name, dim]: symdims) {
            ss << '/' << name << '/' << dim;
        }
        auto key = ss.str();

        auto it = cache.find(key);
        if (it != cache.end()) {
            return it->second.get();
        }

        auto 
            [ assem
            , symbols
            ] = compile_to_assembly
            ( code
            , symdims
            );
        auto prog = std::make_unique<Program<Prog>>();
        prog->prog = Prog::assemble(assem);
        prog->symbols = symbols;

        auto raw_ptr = prog.get();
        cache[key] = std::move(prog);
        return raw_ptr;
    }
};

}
