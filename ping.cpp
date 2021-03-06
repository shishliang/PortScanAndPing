#include "ping.h"

ping::ping()
{

}

// 计算ICMP包的校验和(发送前要用)
unsigned short ping::get_checkSum(unsigned short *buf, int size)
{
    unsigned long sum = 0;
    int len = size/sizeof(unsigned short);

    //先将所有16位相加
    while(len > 1)
    {
        sum += *buf++;
        len--;
    }

    //加最后的字节，如果不是两刚好16位则转为16位
    if(size%sizeof(unsigned short))
    {
        sum += *(unsigned char *)buf;
    }
    else
    {
        sum += *buf;
    }

    //把进位取出来再与低位相加
    while(sum > 0xffff)
    {
        sum = (sum >> 16) + (sum & 0xffff);
    }

    //取反得到检验和
    return (unsigned short)(~sum);
}

// 填充ICMP请求包的具体参数
void ping::pack_icmp(char *icmp_data, int size)
{
    bzero(icmp_data, 1024);

    IcmpHeader *icmp_header = (IcmpHeader *)icmp_data;
    //请求数据报 类型为8
    icmp_header->type = 8;
    icmp_header->code = 0;
    icmp_header->id = (unsigned short) getpid();
    icmp_header->seq = 0;
    icmp_header->check_sum = 0;

    //填充ICMP请求包的数据部分
    char *data = icmp_data + sizeof(IcmpHeader);
    memset(data, 'x', size-sizeof(IcmpHeader));
    //设置检验和
    icmp_header->check_sum = get_checkSum((unsigned short *) icmp_data, size);
}

// 对返回的IP数据包进行解析，定位到ICMP数据
int ping::parse_respone(char *buf, int bytes , const char *ip, int recv_time)
{
    IpHeader *ip_header = (IpHeader *)buf;
    unsigned short ipHeadLen = ip_header->headLen * 4 ;

    if (bytes < ipHeadLen + 8 || ip_header->proto != 1) // ICMP数据不完整, 或者不包含ICMP数据
    {
        return -1;
    }
    //摘去ip数据报的首部
    IcmpHeader *icmpHead = (IcmpHeader*)(buf + ipHeadLen);

    if (icmpHead->type != 0)    // 0表示回应包
    {
        if(icmpHead->type == 11)
        {
            printf("来自 本地ip 的回复：连接超时！\n");
            return -1;
        }
        if(icmpHead->type == 3)
        {
            printf("来自 本地ip 的回复：无法访问目标主机\n");
            return -3;
        }
        return -2;
    }

    if(recv_time >= 0)
    {
        if(recv_time < 1)
        {
            printf(" 来自 %s 的回复： 字节=%d 时间<1ms TTL=%d \n", ip, bytes-ipHeadLen-sizeof(IcmpHeader), ip_header->ttl);
            emit sendMessage(QString("来自 %1 的回复： 字节=%2 时间<1ms TTL=%3 ")
                             .arg(ip).arg(bytes-ipHeadLen-sizeof(IcmpHeader)).arg(ip_header->ttl));
            return 0;
        }
        else if(recv_time >= 1)
        {
            printf("来自 %s 的回复： 字节=%d 时间=%dms TTL=%d \n", ip, bytes-ipHeadLen-sizeof(IcmpHeader), recv_time, ip_header->ttl);
            emit sendMessage(QString("来自 %1 的回复： 字节=%2 时间=%3ms TTL=%4 ")
                             .arg(ip).arg(bytes-ipHeadLen-sizeof(IcmpHeader)).arg(recv_time).arg(ip_header->ttl));
        }

        return recv_time;
    }

    return 0;
}

// 计算最长返回时间
int ping::max(int times[], int n)
{
    int max = times[0];
    int i;
    for(i = 1; i < n; i++)
    {
        max = max > times[i] ? max:times[i];
    }
    return max;

}
// 计算最短返回时间
int ping::min(int times[], int n)
{
    int min = times[0];
    int i;
    for(i = 1; i < n; i++)
    {
        min = min < times[i] ? min:times[i];
    }
    return min;
}

// 计算返回时间的平均值
int ping::average(int times[], int n)
{
    int tatol = 0;
    int i;
    for(i = 0; i < n; i++)
    {
        tatol += times[i];
    }
    return tatol/n;
}
//开始计时
int ping::start_timer(struct timeval *stv)
{
    //获得开始时的当前时间
    gettimeofday(stv, NULL);
    return 0;
}

//结束计时
int ping::stop_timer(struct timeval *stv)
{
    struct timeval etv;
    int time = 0;
    //获得结束时的当前时间
    gettimeofday(&etv, NULL);
    if((etv.tv_usec -= stv->tv_usec) < 0)//微秒
    {
        --etv.tv_sec;
        etv.tv_usec += 1000000;
    }
    etv.tv_sec -= stv->tv_sec;	//毫秒
    time = etv.tv_sec*1000+etv.tv_usec/1000;
    return time;
}


int ping::ping_one(int rawfd, struct sockaddr_in dest_adr, const char *ip, char *icmp_data, int all_time[])
{
    char recv_buf[1024];
    struct timeval stv;
    struct timeval etv;
    int recv_time = 0;
    struct sockaddr_in from_adr;
    socklen_t fromlen = 0;
    int size = sizeof(IcmpHeader)+32;
    int r, i = 0, recv=0, lost=0;
    //发送icmp 请求数据报
    r = sendto(rawfd, icmp_data, size, 0, (struct sockaddr *)&dest_adr, sizeof(dest_adr));
    //开始计时
    start_timer(&stv);
    while(1)
    {
        //清空缓存
        bzero(recv_buf, sizeof(recv_buf));
        //发送icmp 回应数据报
        r = recvfrom(rawfd, recv_buf, 1024, 0, (struct sockaddr *)&from_adr, &fromlen);

        if(r > 0)
        {
            //结束计时
            recv_time = stop_timer(&stv);

            //开始解包
            int ret = parse_respone(recv_buf, r, ip, recv_time);

            if(ret >= 0)//解包正确
            {
                all_time[i] = recv_time;
            }
            else if(ret == -2)	//收到错误包
            {
                i--;
                continue;
            }
            else if(ret < -2)	//未知错误
            {
                break;
                return -1;
            }
            break;
        }
        else if(r == -1)
        {
            return -1;
        }
    }
    //延时1秒
    sleep(1);
    return 0;
}

//执行ping功能
int ping::pingecho(const char *ip,  int send_count)
{
    int rawfd;
    struct sockaddr_in dest_adr;

    char icmp_data[1024];
    int size = sizeof(IcmpHeader)+32;
    int r, i = 0, send, recv=0, lost=0;
    char recv_buf[1024];
    int all_time[1024] = {0};

    //创建原始套接字
    rawfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if(rawfd == -1)
    {
        perror("create socket failed!");
        return -1;
    }

    //设置目的地址与端口
    dest_adr.sin_family = AF_INET;
    dest_adr.sin_port = htons(80);
    inet_aton(ip, &dest_adr.sin_addr);

    //封装icmp数据包
    pack_icmp(icmp_data, size);
    printf("\n正 Ping %s 具有%d个字节的数据：\n", ip, size-sizeof(IcmpHeader));
    emit sendMessage(QString("正 Ping %1 具有%2个字节的数据：").arg(ip).arg(size-sizeof(IcmpHeader)));
    if(send_count == LOOP)
    {
        //无限循环
        while(1)
        {
            if(ping_one(rawfd,dest_adr, ip, icmp_data , all_time) != -1)
            {
                recv++;
            }
            send++;
        }
    }
    else
    {
        for(i = 0; i<send_count; i++)
        {
            if(ping_one(rawfd,dest_adr, ip, icmp_data , all_time) != -1)
            {
                recv++;
            }
            send++;
        }
    }

    printf("\n%s 的 Ping 统计信息：\n   数据包：已发送 = %d，已接收 = %d, 丢失 = %d<%.1f%% 丢失>, \n",
                     ip, send, recv, lost, ((float)lost/(float)send)*100);
    printf("往返行程的估计时间：\n 最短=%dms 最长=%dms 平均 = %dms \n\n"
                ,min(all_time, send), max(all_time, send), average(all_time, send));

//    emit sendMessage(QString("%1 的 Ping 统计信息：").arg(ip));
//    emit sendMessage(QString("   数据包：已发送 = %1，已接收 = %2, 丢失 = %3").arg(send).arg(recv).arg(lost));
//    emit sendMessage(QString("往返行程的估计时间："));
//    emit sendMessage(QString(" 最短=%1ms 最长=%2ms 平均 = %3ms ").arg(min(all_time, send)).arg(max(all_time, send)).arg(average(all_time, send)));
    close(rawfd);
    return 0;
}

//把域名打包成dns数据报的数据部分 如（3www5baidu3com）
int ping::pack_name(char *data,const char *netname, int name_len)
{
    int i = 0;
    int flg = 0, s = 0;
    int len[3];
    len[0] = 0;
    //计算数量
    for(i = 0; i < name_len; i++)
    {
        if(netname[i] == '.')
        {
            len[flg++] = i - s;
            s += len[flg-1]+1;
        }
    }
    len[flg] = i - s;
    i = 0;
    flg = 0;
    data[i++] = len[flg++];
    //加入每段字节的数量
    for(; i < name_len+1; i++)
    {
        if(netname[i-1] == '.')
        {
            data[i] = len[flg++];
        }
        else
        {
            data[i] = netname[i-1];
        }

    }
    data[i] = 0;

    //查询类型为1 即ip查询
    i+=2;
    data[i] = 0x01;
    i+=2;
    //查询类为1
    data[i] = 0x01;
    i+=1;

    return i;
}


int get_seg_name(unsigned char *name_buf, unsigned char *cname,int tmp_len)
{
    int i;
    for(i=0; i< tmp_len; i++)
    {
        cname[i] = *name_buf++;			 //逐个取别名字符
    }
    return i;
}
//解析域名
void parse_dns_name(unsigned char *recv_buf, unsigned char **answer_o, unsigned char *cname)
{
    int tmp_len;

    int d_length;
    int i, k;
    unsigned char *answer = *answer_o;

    d_length = ntohs(*((unsigned short*)answer));//总长度
    answer +=2;							//data length
    i=0;
    for(k=0; k<d_length; k++)
    {

        tmp_len = *answer++;			//名字段长度
        if(i != 0)
            cname[i++]='.';
        if(tmp_len == 0xc0)				//CO转义为复字段
        {
            int tmp  =  *answer++;		//获得偏移指针
            k ++;
            i--;
            while(1)
            {
                tmp_len = recv_buf[tmp++];//跳转到偏移位置
                if(tmp_len == 0) break;	  //偏移位置结果
                cname[i++]='.';
                //填充域名
                get_seg_name(&recv_buf[tmp], cname+i, tmp_len);
                tmp += tmp_len;
                i += tmp_len;
            }
            continue;
        }
        if(tmp_len == 0) break;
        //填充域名
        get_seg_name(answer, cname+i, tmp_len);
        k += tmp_len;
        i += tmp_len;
        //移动指针
        answer += tmp_len;

    }
    cname[i] = '\0';
    *answer_o = answer;

}
//解析IP
void parse_dns_ip(unsigned char **answer_o ,char *get_ip)
{
    unsigned char *answer = *answer_o;
    Ipadr *ip =(Ipadr *)answer;			//取提ip
    sprintf(get_ip, "%u.%u.%u.%u", ip->a, ip->b, ip->c, ip->d);
    *answer_o = answer;

}

int parse_dns_respone(unsigned char *recv_buf,unsigned char **answer_o, int data_len ,char *get_ip, const char *netname)
{
    int asw_type;
    int i,j, k;
    static int fn = 0, fi = 0;
    int r;
    unsigned char cname[40];
    unsigned char *answer = *answer_o;


    answer +=2;//查询域名
    asw_type = ntohs(*((unsigned short*)answer));
    answer +=2;//type

    answer +=2;//class
    answer +=4;//ttl

    if(asw_type == 5) 			//域名包
    {
        bzero(cname, sizeof(cname));
        //解析域名
        parse_dns_name(recv_buf, &answer, cname);
        fn = 1; 							//标记已取得别名
    }
    else if(asw_type == 1)		//ip 回应包
    {
        //解析IP
        parse_dns_ip(&answer ,get_ip);
        if(fn == 1)
            printf("\n----  %s(%s)",cname , get_ip);
        else
            printf("\n----  %s(%s)",netname , get_ip);
        fi = 1;								//标记已取得IP
    }
    *answer_o = answer;
    if(fn != 0 && fi != 0) return 2;			//已取得别名和域名。则结果
    else if(fi != 0) return 1;
    return 0;
}


//执行dns功能
int ping::get_dns(const char *netname, char *buf, int name_len, int size, char *get_ip)
{
    int udpfd;
    struct sockaddr_in addr;
    struct sockaddr_in dest_addr;
    socklen_t len;
    int data_len;
    int r, i;
    struct timeval tv_out;

    //udp 套接字
    udpfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(udpfd == -1)
    {
        perror("udp socket");
        return 0;
    }

    //设置超时为10s
    tv_out.tv_sec = 10;
    tv_out.tv_usec = 0;
    setsockopt(udpfd, SOL_SOCKET, SO_RCVTIMEO, &tv_out, sizeof(tv_out));

    addr.sin_family = AF_INET;
    addr.sin_port   = htons(9999);
    addr.sin_addr.s_addr = INADDR_ANY;

    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port   = htons(DNS_PORT);
    dest_addr.sin_addr.s_addr = inet_addr(DNS_IP);

    r = bind(udpfd, (struct sockaddr *)&addr, sizeof(addr));

    if(r == -1)
    {
        perror("bind udp");
        return 0;
    }

    char sen_buf[256];
    bzero(sen_buf, 256);
    //设置dns数据报的首部，qr==1 为请求数据报
    DnsHeader *dnsheader = (DnsHeader *)sen_buf;
    dnsheader->id = (unsigned short) getpid();
    dnsheader->qr_opcode_aa_tc_rd = 0x01;
    dnsheader->ra_zero_rcode = 0;
    dnsheader->qdcount = 1;
    dnsheader->ancount = 0;
    dnsheader->nscount = 0;
    dnsheader->arcount = 0;

    char *dns_data = sen_buf+sizeof(DnsHeader);
    //打包域名
    data_len = pack_name(dns_data, netname, name_len);

    //发送请求数据报
    r = sendto(udpfd, sen_buf, sizeof(DnsHeader)+data_len , 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if(r == -1)
    {

        return -1;
    }

    //接收回应数据报
    r = recv(udpfd, sen_buf, size-1, 0);
    if(r == -1) return -1;
    DnsHeader *revheader = (DnsHeader *) sen_buf;	//DNS 首部解析
    revheader->qdcount = ntohs(revheader->qdcount);	//请求数
    revheader->ancount = ntohs(revheader->ancount);	//回应数
    unsigned char *answer = (unsigned char*) (sen_buf+sizeof(DnsHeader)+data_len); //跳过问题部分

    for(i=0; i<revheader->ancount; i++)
    {
        r = parse_dns_respone((unsigned char*)sen_buf, &answer, data_len ,get_ip, netname);
        if(r > 0)
            break;
    }
    close(udpfd);
    return 0;
}


int ping::pinglist(const char *startIp,const char *endIp,int send_count){
//    int temp =ipstrtoint(startIp);
//    emit sendMessage(QString("ip_or: %1").arg(startIp));
//    emit sendMessage(QString("ip: %1").arg(temp));
    char char_ip[50];
//    emit sendMessage(QString("ip_char:%1").arg(intToipstr(ipstrtoint(startIp),char_ip)));
    for(int i=ipstrtoint(startIp);i<=ipstrtoint(endIp);i++){
        pingecho(intToipstr(i,char_ip),send_count);
    }
}

int ping::ipstrtoint(const char *ip){
    int   result = 0;
    int   tmp = 0;
    int   shift = 24;
    const char *pEnd = ip;
    const char *pStart = ip;
    while(*pEnd != '\0')
    {
        while(*pEnd != '.' && *pEnd != '\0')
            pEnd++;
        tmp = 0;
        while(pStart < pEnd){
            tmp = tmp * 10 + (*pStart - '0');
            pStart++;
        }
        result += (tmp << shift);shift -= 8;
        if (*pEnd == '\0')break;
        pStart = pEnd + 1;pEnd++;
    }
    return result;
}

const char * ping::intToipstr (const int ip, char *buf){
    sprintf (buf, "%u.%u.%u.%u",
             (uchar) * ((char *) &ip + 3),
             (uchar) * ((char *) &ip + 2),
             (uchar) * ((char *) &ip + 1),
             (uchar) * ((char *) &ip + 0));
    return buf;
}
