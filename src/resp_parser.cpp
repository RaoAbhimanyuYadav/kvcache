#include <resp_parser.hpp>


RESPParser::RESPParser() {}

void RESPParser::feed(const char* chunk, uint size){
    if(pos >= data.size()) clear();

    data.append(chunk, size);
}

bool RESPParser::try_parse(RESPObj &out_obj){
    size_t saved_pos = pos;
    try{
        out_obj = parse();
        return true;
    }catch(const IncompleteFrameException&){
        pos = saved_pos;
        return false;
    }
}


RESPObj RESPParser::parse(){
    if(pos >= data.size()){
        throw std::runtime_error("Unexpected end of data stream");
    }
    char type_byte = data[pos++];
    switch(type_byte){
        case '+': return RESPObj{ parse_simple_string() };
        case '-': return RESPObj{ parse_error() };
        case ':': return RESPObj{ parse_integer() };
        case '$': return parse_bulk_string() ;
        case '*': return parse_array() ;
        default: throw std::runtime_error("Unknow Resp type byte: " + std::to_string(type_byte));
    }
}

/* carriage return line feed*/
std::string_view RESPParser::read_until_crlf(){
    size_t crlf_idx = data.find("\r\n", pos);
    if(crlf_idx == std::string::npos){
        throw IncompleteFrameException();
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
    return std::stoll(std::string(read_until_crlf()));
}
RESPObj RESPParser::parse_bulk_string(){
    int64_t len = parse_integer();
    if(len == -1) return RESPObj{nullptr};
    if(pos + len + 2 > data.size()){
        throw IncompleteFrameException();
    }
    std::string payload(data.substr(pos, len));
    pos+=len;
    if(data.substr(pos, 2) != "\r\n"){
        throw std::runtime_error("Malformed Bulk String: missing trailing CRLF");
    }
    pos += 2;
    return RESPObj{payload};
}
RESPObj RESPParser::parse_array(){
    int64_t size = parse_integer();
    if(size == -1) return RESPObj{nullptr};
    RESPArray currentArray;
    currentArray.reserve(size);
    for(long long i=0; i<size; ++i){
        currentArray.push_back(parse());
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

