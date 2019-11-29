#include "globalLog.h"
#include <QFile>
#include <QTextStream>

GlobalLogDef::GlobalLogDef()
{

}

void GlobalLogDef::writeToFile(QString &str ,int type){
    QFile file("logfile.txt");
    file.open(QIODevice::ReadWrite | QIODevice::Append);
    QTextStream stream(&file);
    if(type == 0){

        stream << str << "";
    }
    else{
        stream << str << "\r\n";

    }

    file.flush();
    file.close();


}
void GlobalLogDef::writeToFile(int &str ,int type){
    QFile file("logfile.txt");
    file.open(QIODevice::ReadWrite | QIODevice::Append);
    QTextStream stream(&file);
    if(type == 0){

        stream << str << "";
    }
    else{
        stream << str << "\r\n";

    }

    file.flush();
    file.close();


}

QString GlobalLogDef::log = "FLUSH";
