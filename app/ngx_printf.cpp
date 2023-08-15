#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>


#include "ngx_global.h"
#include "ngx_macro.h"
#include "ngx_func.h"


//只用于本文件的一些函数声明就放在本文件中
static u_char *ngx_sprintf_num(u_char *buf, u_char *last, uint64_t ui64,u_char zero, uintptr_t hexadecimal, uintptr_t width);

//----------------------------------------------------------------------------------------------------------------------
//对于 nginx 自定义的数据结构进行标准格式化输出,就像 printf,vprintf 一样，我们顺道学习写这类函数到底内部是怎么实现的
//该函数只不过相当于针对ngx_vslprintf()函数包装了一下，所以，直接研究ngx_vslprintf()即可
u_char *ngx_slprintf(u_char *buf, u_char *last, const char *fmt, ...) 
{
    va_list   args;
    u_char   *p;

    va_start(args, fmt); //使args指向起始的参数
    p = ngx_vslprintf(buf, last, fmt, args);
    va_end(args);        //释放args   
    return p;
}

//----------------------------------------------------------------------------------------------------------------------
//和上边的ngx_snprintf非常类似
u_char * ngx_snprintf(u_char *buf, size_t max, const char *fmt, ...)   //类printf()格式化函数，比较安全，max指明了缓冲区结束位置
{
    u_char   *p;
    va_list   args;

    va_start(args, fmt);
    p = ngx_vslprintf(buf, buf + max, fmt, args);
    va_end(args);
    return p;
}

u_char *ngx_vslprintf(u_char *buf, u_char *last,const char *fmt,va_list args)
{
    u_char zero;
    uintptr_t width,sign,hex,frac_width,scale,n;
    int64_t  i64;
    uint64_t ui64;
    u_char*  p;
    double   f;
    uint64_t frac;

    while(*fmt && buf<last){
        if(*fmt=='%'){
            zero=(u_char)((*++fmt =='0')?'0':' ');
            width=0;
            sign=1;
            hex=0;
            frac_width=0;
            i64=0;
            ui64=0;
            while(*fmt >= '0'&& *fmt <= '9'){
                width=width*10+(*fmt++ - '0');
            }
            for( ; ; ){
                switch(*fmt){
                    case 'u':
                        sign=0;
                        fmt++;
                        continue;
                    case 'X':
                        hex=2;
                        sign=0;
                        fmt++;
                        continue;
                    case 'x':
                        hex=1;
                        sign=0;
                        fmt++;
                        continue;
                    case '.':
                        fmt++;
                        while(*fmt >= '0' && *fmt <= '9'){
                            frac_width=frac_width*10+(*fmt++ - '0');
                        }
                        break;
                    default:
                        break;
                }
                break;
            }
            switch(*fmt){
                case '%':
                    *buf++=*fmt++;
                    continue;
                case 'd':
                    if(sign){
                        i64=(int64_t) va_arg(args,int);
                    }
                    else{
                        ui64=(uint64_t) va_arg(args,uint);
                    }
                    break;
                case 'i':
                    if(sign){
                        i64=(int64_t) va_arg(args,intptr_t);
                    }
                    else{
                        ui64=(uint64_t) va_arg(args,uintptr_t);
                    }
                    break;
                case 'L':
                    if (sign){
                        i64 = va_arg(args, int64_t);
                    } 
                    else {
                        ui64 = va_arg(args, uint64_t);
                    }
                    break;
                case 'p':
                    ui64=(uintptr_t) va_arg(args,void*);
                    sign=0;
                    hex=2;
                    zero='0';
                    width=2*sizeof(void*);
                    break;
                case 's':
                    p=va_arg(args,u_char*);
                    while(*p && buf<last){
                        *buf++=*p++;
                    }
                    fmt++;
                    continue;
                case 'P':
                    i64= (int64_t)va_arg(args,pid_t);
                    sign=1;
                    break;
                case 'f':
                    f=va_arg(args,double);
                    if(f<0){
                        *buf++ = '-';
                        f=-f;
                    }
                    ui64=(int64_t)(f);
                    frac=0;
                    if(frac_width){
                        scale=1;
                        for(int n=frac_width;n;n--){
                            scale*=10;
                        }
                        frac=(uint64_t)((f-(double)ui64)*scale+0.5);
                        if(scale==frac){
                            ui64++;
                            frac=0;
                        }
                    }
                    buf = ngx_sprintf_num(buf, last, ui64, zero, 0, width);
                    if(frac_width){
                        if(buf<last){
                            *buf++ = '.';
                        }
                        buf = ngx_sprintf_num(buf, last, frac, '0', 0, frac_width);
                    }
                    fmt++;
                    continue;
                default:
                    *buf++=*fmt++;
                    continue;
            }
            if(sign){
                if(i64 < 0){
                    *buf++ = '-';
                    ui64=(uint64_t) -i64;
                }
                else{
                    ui64=(uint64_t) i64;
                }
            }
            buf = ngx_sprintf_num(buf, last, ui64, zero, hex, width);
            fmt++;

        }
        else{
            *buf++=*fmt++;
        }
    }
    return buf;
}


static u_char * ngx_sprintf_num(u_char *buf, u_char *last, uint64_t ui64, u_char zero, uintptr_t hexadecimal, uintptr_t width)
{
    //temp[21]
    u_char      *p, temp[NGX_INT64_LEN + 1];   //#define NGX_INT64_LEN   (sizeof("-9223372036854775808") - 1)     = 20   ，注意这里是sizeof是包括末尾的\0，不是strlen；             
    size_t      len;
    uint32_t    ui32;

    static u_char   hex[] = "0123456789abcdef";  //跟把一个10进制数显示成16进制有关，换句话说和  %xd格式符有关，显示的16进制数中a-f小写
    static u_char   HEX[] = "0123456789ABCDEF";  //跟把一个10进制数显示成16进制有关，换句话说和  %Xd格式符有关，显示的16进制数中A-F大写

    p = temp + NGX_INT64_LEN; //NGX_INT64_LEN = 20,所以 p指向的是temp[20]那个位置，也就是数组最后一个元素位置

    if (hexadecimal == 0)  
    {
        if (ui64 <= (uint64_t) NGX_MAX_UINT32_VALUE)   //NGX_MAX_UINT32_VALUE :最大的32位无符号数：十进制是‭4294967295‬
        {
            ui32 = (uint32_t) ui64; //能保存下
            do  //这个循环能够把诸如 7654321这个数字保存成：temp[13]=7,temp[14]=6,temp[15]=5,temp[16]=4,temp[17]=3,temp[18]=2,temp[19]=1
                  //而且的包括temp[0..12]以及temp[20]都是不确定的值
            {
                *--p = (u_char) (ui32 % 10 + '0');  //把屁股后边这个数字拿出来往数组里装，并且是倒着装：屁股后的也往数组下标大的位置装；
            }
            while (ui32 /= 10); //每次缩小10倍等于去掉屁股后边这个数字
        }
        else
        {
            do 
            {
                *--p = (u_char) (ui64 % 10 + '0');
            } while (ui64 /= 10); //每次缩小10倍等于去掉屁股后边这个数字
        }
    }
    else if (hexadecimal == 1)  //如果显示一个十六进制数字，格式符为：%xd，则这个条件成立，要以16进制数字形式显示出来这个十进制数,a-f小写
    {
        //比如我显示一个1,234,567【十进制数】，他对应的二进制数实际是 12 D687 ，那怎么显示出这个12D687来呢？
        do 
        {            
            //0xf就是二进制的1111,大家都学习过位运算，ui64 & 0xf，就等于把 一个数的最末尾的4个二进制位拿出来；
            //ui64 & 0xf  其实就能分别得到 这个16进制数也就是 7,8,6,D,2,1这个数字，转成 (uint32_t) ，然后以这个为hex的下标，找到这几个数字的对应的能够显示的字符；
            *--p = hex[(uint32_t) (ui64 & 0xf)];    
        } while (ui64 >>= 4);    //ui64 >>= 4     --->   ui64 = ui64 >> 4 ,而ui64 >> 4是啥，实际上就是右移4位，就是除以16,因为右移4位就等于移动了1111；
                                 //相当于把该16进制数的最末尾一位干掉，原来是 12 D687, >> 4后是 12 D68，如此反复，最终肯定有=0时导致while不成立退出循环
                                  //比如 1234567 / 16 = 77160(0x12D68) 
                                  // 77160 / 16 = 4822(0x12D6)
    } 
    else // hexadecimal == 2    //如果显示一个十六进制数字，格式符为：%Xd，则这个条件成立，要以16进制数字形式显示出来这个十进制数,A-F大写
    { 
        //参考else if (hexadecimal == 1)，非常类似
        do 
        { 
            *--p = HEX[(uint32_t) (ui64 & 0xf)];
        } while (ui64 >>= 4);
    }

    len = (temp + NGX_INT64_LEN) - p;  //得到这个数字的宽度，比如 “7654321”这个数字 ,len = 7

    while (len++ < width && buf < last)  //如果你希望显示的宽度是10个宽度【%12f】，而实际想显示的是7654321，只有7个宽度，那么这里要填充5个0进去到末尾，凑够要求的宽度
    {
        *buf++ = zero;  //填充0进去到buffer中（往末尾增加），比如你用格式  
                                          //ngx_log_stderr(0, "invalid option: %10d\n", 21); 
                                          //显示的结果是：nginx: invalid option:         21  ---21前面有8个空格，这8个弄个，就是在这里添加进去的；
    }
    
    len = (temp + NGX_INT64_LEN) - p; //还原这个len，也就是要显示的数字的实际宽度【因为上边这个while循环改变了len的值】
    //现在还没把实际的数字比如“7654321”往buf里拷贝呢，要准备拷贝

    //如下这个等号是我加的【我认为应该加等号】，nginx源码里并没有加;***********************************************
    if((buf + len) >= last)   //发现如果往buf里拷贝“7654321”后，会导致buf不够长【剩余的空间不够拷贝整个数字】
    {
        len = last - buf; //剩余的buf有多少我就拷贝多少
    }

    return ngx_cpymem(buf, p, len); //把最新buf返回去；
}
