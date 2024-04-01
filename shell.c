#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>

#define MAXCMDLEN 20
#define BUFFERSIZE 16

#define CAN(i) i % 2 //текущий канал
#define PREVCAN(i) abs(i - 1) % 2 //предыдущий канал

/*
    * Функция для анализа входной строки и разделения её на команды
    * Вычисление условий перенаправления ввода/вывода
    * Моделирование фонового процесса:
    *   1) он работает паралелльно с основным (то есть после его запуска,
    *      можно вводить команды, не дожидаясь конца фонового)
    *   2) он не реагирует на сигналы, приходящие с клавиатуры
    *   3) он не должен читать со стандартного ввода
    *
    * Запуск Конвейера. Работает в качестве командного интерпретатора
    * подобно терминалу Unix
*/

char *read_str(int *len);
/*
    * Считывание команды со стандартного потока
*/

char *read_name(char *cmd_str);
/*
    * Считывание имени файла из строки
*/

void delete(char **cmd, int flags_num);
/*
    * Очищение памяти
*/

char **takecmd(char *cmd_str, int strl, int *ind, int *flags_num, int *cont);
/*
    * Взятие одной команды из конвейера для выполнения
*/

int infile(char *cmd_str, int ssize, int *infile);
/*
    * Входной файл
*/

int outfile(char *cmd_str, int ssize, int *outfile);
/*
    * Выходной файл
*/

int fonmode(char *cmd_str, int ssize);
/*
    * Фоновый режим
*/
void run_cmd(char *cmd_str, int len);
/*
    * Запуск конвейера
*/

char *define_dir(int *wlen);
/*
    * Определение текущей директории
*/

int main(int argc, char **argv)
{
    int exit_con = 0, pid = 1, len = 0;
    char *cmd_str, *dir;
    int fdin, fdout;
    int dlen = 0;
    fdin = fdout = 0;
    dir = define_dir(&dlen);
    while (!exit_con)
    {
        printf("shell:~$ ");
        fflush(stdout);
        if (!(pid = fork())) // если находимся в сыновьем процессе
        {
            cmd_str = read_str(&len);
            if (!strncmp(cmd_str, "exit", 4)) // если ма считали комманду к выходу
            {
                kill(getppid(), SIGINT);
                free(cmd_str);
                free(dir);
                exit(EXIT_SUCCESS);
            }
            if (fonmode(cmd_str, len)) //если есть комманда фонового процесса
            {
               fdin = open("/dev/null", O_RDONLY | O_CREAT);
                dup2(fdin, 0);//перенаправляем вход в пустоту
                close(fdin);
                fdin = open("/dev/null", O_WRONLY | O_CREAT);
                dup2(fdin, 1); //перенаправляем вывод в пустоту
                dup2(fdin, 2);// перенаправляем вывод ошибок
                signal(SIGINT, SIG_IGN);// игнорируем прерывание с терминала
                if (fork()) //если отцовский процесс
                    exit(0);
            }
            if (infile(cmd_str, len, &fdin)) //если есть входной файл
             dup2(fdin, 0); //перенаправляем входной поток
            run_cmd(cmd_str, len);
            free(cmd_str);
            if (fdin)
                close(fdin);
            fdin = 0;
            while(waitpid(-1, NULL, 0) > 0);
            free(dir);
            exit(EXIT_SUCCESS);
        }
        else
        {
            if (pid < 0)
                perror("process");
            waitpid(pid, NULL, 0);
        }
    }
    free(dir);
    return 0;
}

char *read_str(int *len)
{
    int rd_bytes, maxlen = 16;
    char *cmd_str;
    cmd_str = calloc(maxlen, sizeof(*cmd_str));
    while ((rd_bytes = read(0, cmd_str + *len, BUFFERSIZE)) > 0)
    {
        *len += rd_bytes;
        if (*len >= maxlen)
            cmd_str = realloc(cmd_str, (maxlen *= 2) * sizeof(*cmd_str));
        if (cmd_str[*len - 1] == '\n')
            break;
    }
    return cmd_str;
}

void delete(char **cmd, int flags_num)
{
    int i = 0;
    for (; i <= flags_num; i++)
        free(cmd[i]);
    free(cmd);
}

char **takecmd(char *cmd_str, int strl, int *ind, int *flags_num, int *cont) //выполнение команды из конвеера
{
    int exit_con, maxhigh = 4, maxlen = 4, space, high, len;
    char **cmd;
    high = len = space = exit_con = 0;
    cmd = calloc(maxhigh, sizeof(*cmd));
    cmd[high] = calloc(maxlen, sizeof(**cmd));
    while (isspace(cmd_str[*ind]) || cmd_str[*ind] == '|')
        //проматываем пробелы перед командой и спец символ
        *ind += 1;
    while (!exit_con)
    {
        if (isspace(cmd_str[*ind]))
        {
            space++;
            if (high >= maxhigh - 1)
                cmd = realloc(cmd, (maxhigh *= 2) * sizeof(*cmd));
            if (len)
            {
                cmd[high][len] = '\0';
                cmd[++high] = calloc(maxlen, sizeof(**cmd));
                len = 0;
            }
        }
        else if (space && (cmd_str[*ind] == '|' || cmd_str[*ind] == '<'
                    || cmd_str[*ind] == '>' || cmd_str[*ind] == '&'))
        {
            while (cmd_str[*ind] != '|' && cmd_str[*ind] != '\0' && cmd_str[*ind] != '&' && cmd_str[*ind] != '>')
                *ind += 1;
            if (cmd_str[*ind] == '|')
            {
                *cont = 1;
            }
            cmd[high] = NULL;
            exit_con++;
        }
        else
        {
            space = 0;
            cmd[high][len++] = cmd_str[*ind];
            if (len >= maxlen - 1)
                cmd[high] = realloc(cmd[high], (maxlen *= 2) * sizeof(**cmd));
        }
        if (!exit_con)
            *ind += 1;
        if (*ind == strl)
        {
            exit_con++;
            cmd[high] = NULL;
        }
    }
    *flags_num = high;
    return cmd;
}

int infile(char *cmd_str, int ssize, int *infile)
{
    int len = 0;
    char insymbol = '<';
    char *name;
    while (cmd_str[len] != '\n') //пробегаемся по всей строке
    {
        if (cmd_str[len++] == insymbol) // если встретили <
        {
            while (isspace(cmd_str[len])) // проскакиваем пробелы
            {
                len++;
                if (len >= ssize)  //вышли за границы строки
                {
                    //нет имени входного файла => ошибка, которую нужно обработать
                    //Следует здесь отправить сигнал самому себе об аварийном завершении
                }
            }
            name = read_name(cmd_str + len); //считали имя файла
            if ((*infile = open(name, O_RDONLY)) < 0)
            {
                //не удалось открыть файл для чтения
                perror(name);
                *infile = -1;
            }
            free(name);
        }
    }
    if (*infile > 0)
        return 1;
    return 0;
}

int outfile(char *cmd_str, int ssize, int *outfile)
{
    int len = 0, append = 0;
    char outsymbol = '>';
    char *name;
    while (cmd_str[len] && cmd_str[len] != '\n')
    {
        if (cmd_str[len++] == outsymbol)
        {
            if (cmd_str[len] == outsymbol)
            {
                append++;
                len++;
            }
            while (isspace(cmd_str[len]))
            {
                len++;
                if (len >= ssize)
                {
                    //нет имени входного файла => ошибка, которую нужно обработать
                    //Следует здесь отправить сигнал самому себе об аварийном завершении
                    fprintf(stderr, "There is not a file name\n");
                    kill(getpid(), SIGKILL);
                }
            }
            name = read_name(cmd_str + len); //считали имя файла
            if (append && (*outfile = open(name, O_WRONLY | O_CREAT |  O_APPEND, 0760)) < 0)
            {
                //не удалось открыть файл для записи
                perror(name);
                *outfile = -1;
            }
            if (!append && (*outfile = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0760)) < 0)
            {
                //не удалось открыть файл для записи
                perror(name);
                *outfile = -1;
            }
            free(name);
        }
    }
    if (*outfile > 0)
        return 1;
    return 0;
}

int fonmode(char *cmd_str, int ssize)
{
    int len = 0;
    while (*(cmd_str + len) != '\n')
    {
        if (*(cmd_str + len) == '&')
            return 1;
        len++;
    }
    return 0;
}

void run_cmd(char *cmd_str, int len)
{
    int ind = 0, flags_num, exit_con = 0;
    int cont = 1, fdout = 1;
    char **cmd;
    int can[2][2] = {{0, 1}, {0, 1}};
    unsigned num = 0;
    while (!exit_con)
    {
        cont = 0;
        cmd = takecmd(cmd_str, len, &ind, &flags_num, &cont);
        if (can[CAN(num)][0] != 0)
            close(can[CAN(num)][0]);
        if (can[CAN(num)][1] != 1)
            close(can[CAN(num)][1]);
        pipe(can[CAN(num)]);
        //The last cmd
        if (!cont)
        {
            while(wait(NULL) != -1);
            close(can[CAN(num)][0]);
            exit_con++;
            if (outfile(cmd_str, len, &fdout))
                dup2(fdout, 1);
            else
            {
                close(can[CAN(num)][1]);
                can[CAN(num)][1] = 1;
            }
            execvp(*cmd, cmd);
            delete(cmd, flags_num);
            perror("exec");
            exit(EXIT_FAILURE);
        }
        else if (!fork())
        {
            close(can[CAN(num)][0]);
            dup2(can[CAN(num)][1], 1);
            execvp(*cmd, cmd);
            delete(cmd, flags_num);
            perror("exec");
            exit(EXIT_FAILURE);
        }
        close(can[CAN(num)][1]);
        dup2(can[CAN(num)][0], 0);
        num++; //сдвигаем счетчик каналов
        delete(cmd, flags_num);
    }
}

char *define_dir(int *wlen)
{
    int fd[2], can[2];
    int slash = 0, index = 0;
    char *word;
    word = calloc(100, sizeof(*word));
    pipe(fd);
    if (!fork())
    {
        pipe(can);
        if (!fork())
        {
            close(can[0]);
            dup2(can[1], 1);
            execlp("pwd", "pwd", NULL);
            perror("dir");
            exit(0);
        }
        close(can[1]);
        read(can[0], word, 100);
        close(fd[0]);
        while (slash <= 2 && index < 100)
        {
            if (word[index] == '/')
                slash++;
            index++;
        }
        *wlen = strlen(word) - index + 1;
        write(fd[1], wlen, sizeof(*wlen));
        write(fd[1], word + index - 1, *wlen * sizeof(*word));
        close(fd[1]);
        wait(NULL);
        exit(0);
    }
    close(fd[1]);
    read(fd[0], wlen, sizeof(*wlen));
    read(fd[0], word, *wlen);
    word[--(*wlen)] = '\0';
    close(fd[0]);
    wait(NULL);
    return word;
}

char *read_name(char *cmd_str)
{
    int ind = 0, len;
    char *name;
    len = strlen(cmd_str);
    name = calloc(len, sizeof(*name));
    while (ind < len && cmd_str[ind] != '|' && cmd_str[ind] != ' '
            && cmd_str[ind] != '\n' && cmd_str[ind] != '<' && cmd_str[ind] != '>')
    {
                name[ind] = cmd_str[ind];
                ind++;
    }
    return name;
}
