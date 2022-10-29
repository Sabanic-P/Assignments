./${1} &
server_pid=$! 
(./${2} & ./${2} & ./${2} & ./${2} & ./${2} & ./${2} & wait)
kill -2 $server_pid