#ifndef GLOBALLOG_H
#define GLOBALLOG_H

#include <QString>

class GlobalLogDef  {
public:
    GlobalLogDef();
    static void writeToFile(QString &str,int type);
    static void writeToFile(int &str,int type);
    static QString  log;
};

#endif // GLOBALLOG_H


