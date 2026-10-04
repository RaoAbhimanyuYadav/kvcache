#include <resp_parser.hpp>

#include <charconv>
#include <system_error>

namespace {

constexpr std::size_t kParserCompactionThreshold = 1024;
constexpr std::size_t kMaxRespLineLength = 64;
constexpr std::size_t kMaxBulkStringLength = 64;
constexpr std::int64_t kMaxArrayLength = 4;
constexpr std::size_t kMaxNestingDepth = 1;

}  // namespace

RESPParser::RESPParser() {}

void RESPParser::feed(const char* chunk, std::size_t size){
    if (pos >= data.size()) {
        clear();
    } else if (pos >= kParserCompactionThreshold && pos >= data.size() / 2) {
        // Compact only after a meaningful prefix has been consumed. This
        // avoids shifting the remaining pipeline once for every parsed frame.
        data.erase(0, pos);
        pos = 0;
    }

    data.append(chunk, size);
}

ParseResult RESPParser::try_parse(RESPObj &out_obj){
    if(pos == data.size()) return ParseResult::Failure;
    size_t saved_pos = pos;
    try{
        out_obj = parse();
        return ParseResult::Success;
    }catch(const IncompleteFrameException&){
        pos = saved_pos;
        return ParseResult::Failure;
    }catch(const MalformedFrameException& err){
        out_obj = RESPObj{
            RESPArray{
                RESPObj{"ERR"}, RESPObj{std::string(err.what())}
            }
        };
        std::cerr<<"Error in Parse, "<<err.what()<<"\n";
        // Report a malformed frame once and discard the buffered remainder.
        // The server closes this connection after receiving the error object.
        pos = data.size();
        return ParseResult::Rejection;
    }catch(...){
        pos = saved_pos;
        std::cerr<<"Error in Parse, not known type error\n";
        return ParseResult::Rejection;
    }
}


RESPObj RESPParser::parse(std::size_t depth){
    if (depth > kMaxNestingDepth) {
        throw MalformedFrameException("RESP nesting limit exceeded");
    }
    if(pos == data.size()) throw IncompleteFrameException();
    if(pos > data.size()) throw MalformedFrameException("Unexpected end of data stream");
    
    char type_byte = data[pos++];
    switch(type_byte){
        case '+': return RESPObj{ parse_simple_string() };
        case '-': return RESPObj{ parse_error() };
        case ':': return RESPObj{ parse_integer() };
        case '$': return parse_bulk_string() ;
        case '*': return parse_array(depth) ;
        default: throw MalformedFrameException("Unknow Resp type byte: " + std::to_string(type_byte));
    }
}

/* carriage return line feed*/
std::string_view RESPParser::read_until_crlf(){
    size_t crlf_idx = data.find("\r\n", pos);
    if(crlf_idx == std::string::npos){
        if (data.size() - pos > kMaxRespLineLength) {
            throw MalformedFrameException("RESP line exceeds the maximum length");
        }
        throw IncompleteFrameException();
    }
    if (crlf_idx - pos > kMaxRespLineLength) {
        throw MalformedFrameException("RESP line exceeds the maximum length");
    }
    std::string_view line = std::string_view(data).substr(pos, crlf_idx - pos);
    pos = crlf_idx + 2;
    return line;
}

std::string RESPParser::parse_simple_string(){
    return std::string(read_until_crlf());
}
std::string RESPParser::parse_error(){
    return "Error: " + std::string(read_until_crlf());
}

int64_t RESPParser::parse_integer(){
    const std::string_view line = read_until_crlf();
    if (line.empty()) throw MalformedFrameException("Expected a valid integer");

    std::int64_t value = 0;
    const char* begin = line.data();
    const char* end = begin + line.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        throw MalformedFrameException("Expected a valid integer");
    }
    return value;
}
RESPObj RESPParser::parse_bulk_string(){
    const std::int64_t len = parse_integer();
    if (len == -1) return RESPObj{nullptr};
    if (len < 0) throw MalformedFrameException("Invalid bulk string length");
    if (len > kMaxBulkStringLength) {
        throw MalformedFrameException("Bulk string exceeds the maximum length");
    }

    const std::size_t payload_len = static_cast<std::size_t>(len);
    if (pos > data.size() || data.size() - pos < payload_len ||
        data.size() - pos - payload_len < 2) {
        throw IncompleteFrameException();
    }
    std::string payload(data.substr(pos, payload_len));
    pos += payload_len;
    if(data.substr(pos, 2) != "\r\n"){
        throw MalformedFrameException("Malformed Bulk String: missing trailing CRLF");
    }
    pos += 2;
    return RESPObj{payload};
}
RESPObj RESPParser::parse_array(std::size_t depth){
    const std::int64_t size = parse_integer();
    if (size == -1) return RESPObj{nullptr};
    if (size < 0) throw MalformedFrameException("Invalid array length");
    if (size > kMaxArrayLength) {
        throw MalformedFrameException("Array exceeds the maximum element count");
    }

    RESPArray currentArray;
    currentArray.reserve(static_cast<std::size_t>(size));
    for(std::int64_t i = 0; i < size; ++i){
        currentArray.push_back(parse(depth + 1));
    }
    return RESPObj{currentArray};
}

void  RESPParser::extract_resp_obj(const RESPObj &obj, int depth) const{
    std::string indent(2*depth, ' ');
    visit(overloaded {
        [&](const std::string &str){
            std::cout<<indent<<str<<std::endl;
        },
        [&](const RESPArray &vec){
            for(const RESPObj& cur:vec){
                extract_resp_obj(cur, depth+1);
            }
        },
        [&](const int64_t val){
            std::cout<<indent<<val<<std::endl;
        },
        [&](std::nullptr_t ){
            std::cout<<indent<<"NULL\n";
        }
    }, obj.value);
}

std::vector<std::string> RESPParser::get_command_array(const RESPObj &obj) const{
    return std::visit([](auto &&arg) -> std::vector<std::string>{
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, RESPArray>){
            std::vector<std::string> rv;
            for(const auto &item: arg){
                if (std::holds_alternative<std::string>(item.value)){
                    rv.push_back(std::get<std::string>(item.value));
                }else {
                    std::cout<<"REQUEST:: Array Element are not string\n";
                    return {};
                }
            }
            return rv;
        }else{
            std::cout<<"REQUEST:: Parsed obj is not array\n"; 
        }
        return {};
    }, obj.value);
}

void print_resp(const RESPObj &obj){
    std::visit([](auto &&arg){
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>){
            std::cout << "String/Error :" << arg << "\n";
        }else if constexpr (std::is_same_v<T, int64_t>){
            std::cout << "Integer: " << arg << "\n";
        }else if constexpr (std::is_same_v<T, RESPArray>) {
            std::cout << "Array [ \n";
            for (const auto& item : arg) {
                std::cout << "  ";
                print_resp(item);
            }
            std::cout << "]\n";
        }
    }, obj.value);
}

