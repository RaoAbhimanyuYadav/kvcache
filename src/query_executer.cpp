#include "query_executer.hpp"

std::string QueryExecuter::execute(const std::vector<std::string> &args){
    try{
        return try_execution(args);
    }catch(const InvalidNumberOfArgumentsExecption& err){
        return err.what();
    }catch(const InvalidArgumentsLengthExecption& err){
        return err.what();
    }catch(const FailedToExecuteExecption& err){
        return err.what();
    }
}

std::string QueryExecuter::try_execution(const std::vector<std::string> &args){
    if(args.size() == 0) throw InvalidNumberOfArgumentsExecption();
    const std::string& cmd = args[0];
    if(cmd == "PING"){
        return "+PONG\r\n";
    }
    if(cmd == "SET") {
        if(args.size() != 3) throw InvalidNumberOfArgumentsExecption();
        if(args[1].size() >20 || args[2].size() > 20) throw InvalidArgumentsLengthExecption();
        if(store.set_key_value(args[1], args[2])) return "+OK\r\n";
        throw FailedToExecuteExecption();
    }
    if(cmd == "GET"){
        if(args.size() != 2) throw InvalidNumberOfArgumentsExecption();
        auto const [success, value] = store.get_value(args[1]);
        if(success){
            return ("$" + std::to_string(value.size()) + "\r\n" + value + "\r\n");
        }
        return "$-1\r\n";
    }
    if(cmd == "DEL"){
        if(args.size() != 2) throw InvalidNumberOfArgumentsExecption();
        auto success = store.del_key_val(args[1]);
        if(success){
            return ":1\r\n";
        }
        return ":0\r\n";
    }
    if(cmd == "EXISTS"){
        if(args.size() != 2) throw InvalidNumberOfArgumentsExecption();
        auto success = store.exists_key(args[1]);
        if(success){
            return ":1\r\n";
        }
        return ":0\r\n";
    }
    std::cerr<<"User try to run this command: "<<cmd<<"\n";
    return "-ERR Unsupported Command\r\n";
}
    
