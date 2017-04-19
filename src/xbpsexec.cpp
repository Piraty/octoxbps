/*
* This file is part of OctoXBPS, an open-source GUI for pacman.
* Copyright (C) 2015 Alexandre Albuquerque Arnt
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
*/

#include "xbpsexec.h"
#include "strconstants.h"
#include "unixcommand.h"
#include "wmhelper.h"

#include <QRegularExpression>
#include <QDebug>

/*
 * This class decouples pacman commands executing and parser code from Octopi's interface
 */

/*
 * Let's create the needed unixcommand object that will ultimately execute Pacman commands
 */
XBPSExec::XBPSExec(QObject *parent) : QObject(parent)
{
  m_unixCommand = new UnixCommand(parent);
  m_iLoveCandy = UnixCommand::isILoveCandyEnabled();
  m_debugMode = false;

  QObject::connect(m_unixCommand, SIGNAL( started() ), this, SLOT( onStarted()));

  QObject::connect(m_unixCommand, SIGNAL( finished ( int, QProcess::ExitStatus )),
                   this, SLOT( onFinished(int, QProcess::ExitStatus)));

  QObject::connect(m_unixCommand, SIGNAL( startedTerminal()), this, SLOT( onStarted()));

  QObject::connect(m_unixCommand, SIGNAL( finishedTerminal( int, QProcess::ExitStatus )),
                   this, SLOT( onFinished(int, QProcess::ExitStatus)));

  QObject::connect(m_unixCommand, SIGNAL( readyReadStandardOutput()),
                   this, SLOT( onReadOutput()));

  QObject::connect(m_unixCommand, SIGNAL( readyReadStandardError() ),
                   this, SLOT( onReadOutputError()));
}

/*
 * Let's remove UnixCommand temporary file...
 */
XBPSExec::~XBPSExec()
{
  m_unixCommand->removeTemporaryFile();
}

/*
 * Turns DEBUG MODE on or off
 */
void XBPSExec::setDebugMode(bool value)
{
  m_debugMode = value;
}

/*
 * Removes Octopi's temporary transaction file
 */
void XBPSExec::removeTemporaryFile()
{
  m_unixCommand->removeTemporaryFile();
}

/*
 * Searches for the presence of the db.lock file
 */
bool XBPSExec::isDatabaseLocked()
{
  QString lockFilePath("/var/lib/pacman/db.lck");
  QFile lockFile(lockFilePath);

  return (lockFile.exists());
}

/*
 * Removes Pacman DB lock file
 */
void XBPSExec::removeDatabaseLock()
{
  UnixCommand::execCommand("rm /var/lib/pacman/db.lck");
}

/*
 * Searches the given output for a series of verbs that a Pacman transaction may produce
 */
bool XBPSExec::searchForKeyVerbs(QString output)
{
  return (output.contains(QRegExp("checking ")) ||
          output.contains(QRegExp("loading ")) ||
          output.contains(QRegExp("installing ")) ||
          output.contains(QRegExp("upgrading ")) ||
          output.contains(QRegExp("downgrading ")) ||
          output.contains(QRegExp("resolving ")) ||
          output.contains(QRegExp("looking ")) ||
          output.contains(QRegExp("removing ")));
}

/*
 * Breaks the output generated by QProcess so we can parse the strings
 * and give a better feedback to our users (including showing percentages)
 *
 * Returns true if the given output was split
 */
bool XBPSExec::splitOutputStrings(QString output)
{
  bool res = true;
  QString msg = output.trimmed();
  QStringList msgs = msg.split(QRegExp("\\n"), QString::SkipEmptyParts);

  foreach (QString m, msgs)
  {
    QStringList m2 = m.split(QRegExp("\\(\\s{0,3}[0-9]{1,4}/[0-9]{1,4}\\) "), QString::SkipEmptyParts);

    if (m2.count() == 1)
    {
      //Let's try another test... if it doesn't work, we give up.
      QStringList maux = m.split(QRegExp("%"), QString::SkipEmptyParts);
      if (maux.count() > 1)
      {
        foreach (QString aux, maux)
        {
          aux = aux.trimmed();
          if (!aux.isEmpty())
          {
            if (aux.at(aux.count()-1).isDigit())
            {
              aux += "%";
            }

            if (m_debugMode) qDebug() << "_split - case1: " << aux;
            parseXBPSProcessOutput(aux);
          }
        }
      }
      else if (maux.count() == 1)
      {
        if (!m.isEmpty())
        {
          if (m_debugMode) qDebug() << "_split - case2: " << m;
          parseXBPSProcessOutput(m);
        }
      }
    }
    else if (m2.count() > 1)
    {
      foreach (QString m3, m2)
      {
        if (!m3.isEmpty())
        {
          if (m_debugMode) qDebug() << "_split - case3: " << m3;
          parseXBPSProcessOutput(m3);
        }
      }
    }
    else res = false;
  }

  return res;
}

/*
 * Processes the output of the 'pacman process' so we can update percentages and messages at real time
 */
void XBPSExec::parseXBPSProcessOutput(QString output)
{
  if (m_commandExecuting == ectn_RUN_IN_TERMINAL ||
      m_commandExecuting == ectn_RUN_SYSTEM_UPGRADE_IN_TERMINAL) return;

  bool continueTesting = false;
  QString perc;
  QString msg = output;
  QString progressRun;
  QString progressEnd;
  QString target;

  msg.remove(QRegularExpression(".+\\[Y/n\\].+"));

  //Let's remove color codes from strings...
  msg.remove("\033[0;1m");
  msg.remove("\033[0m");
  msg.remove("[1;33m");
  msg.remove("[00;31m");
  msg.remove("\033[1;34m");
  msg.remove("\033[0;1m");
  msg.remove("c");
  msg.remove("C");
  msg.remove("");
  msg.remove("[m[0;37m");
  msg.remove("o");
  msg.remove("[m");
  msg.remove(";37m");
  msg.remove("[c");
  msg.remove("[mo");
  msg.remove("[1A[K");

  if (m_debugMode) qDebug() << "_treat: " << msg;

  progressRun = "%";
  progressEnd = "100%";

  //If it is a percentage, we are talking about curl output...
  if(msg.indexOf(progressEnd) != -1)
  {
    perc = "100%";
    emit percentage(100);
    continueTesting = true;
  }

  if ((msg.contains(".xbps:") || msg.contains(".xbps.sig:")) && msg.contains("%"))
  {
    //We're dealing with packages being downloaded
    int colon = msg.indexOf(":");
    target = msg.left(colon);

    if(!m_textPrinted.contains(target))
      prepareTextToPrint("<b><font color=\"#FF8040\">" + target + "</font></b>");
  }
  else if (msg.contains("Updating") &&
            (!msg.contains(QRegularExpression("B/s")) && (!msg.contains(QRegularExpression("configuration file")))))
  {
    int p = msg.indexOf("'");
    if (p == -1) return; //Guard!

    target = msg.left(p).remove("Updating `").trimmed();
    target.remove("[*] ");

    if(!m_textPrinted.contains(target))
    {
      prepareTextToPrint("Updating " + target); //, ectn_DONT_TREAT_URL_LINK);
    }

    return;
  }

  if (msg.indexOf(progressRun) != -1 || continueTesting)
  {
    int p = msg.indexOf("%");
    if (p == -1 || (p-3 < 0) || (p-2 < 0)) return; //Guard!

    if (msg.at(p-2).isSpace())
      perc = msg.mid(p-1, 2).trimmed();
    else if (msg.at(p-3).isSpace())
      perc = msg.mid(p-2, 3).trimmed();

    if (m_debugMode) qDebug() << "percentage is: " << perc;

    //Here we print the transaction percentage updating
    if(!perc.isEmpty() && perc.indexOf("%") > 0)
    {
      int ipercentage = perc.left(perc.size()-1).toInt();
      emit percentage(ipercentage);
    }
  }
  //It's another error, so we have to output it
  else
  {
    if (msg.contains(QRegularExpression("ETA")) ||
      msg.contains(QRegularExpression("KiB")) ||
      msg.contains(QRegularExpression("MiB")) ||
      //msg.contains(QRegularExpression("KB/s")) ||
      msg.contains(QRegularExpression("B/s")) ||
      msg.contains(QRegularExpression("[0-9]+ B")) ||
      msg.contains(QRegularExpression("[0-9]{2}:[0-9]{2}"))) return;

    //Let's supress some annoying string bugs...
    msg.remove(QRegularExpression("\\(process.+"));
    msg.remove(QRegularExpression("Using the fallback.+"));
    msg.remove(QRegularExpression("Gkr-Message:.+"));
    msg.remove(QRegularExpression("kdesu.+"));
    msg.remove(QRegularExpression("kbuildsycoca.+"));
    msg.remove(QRegularExpression("Connecting to deprecated signal.+"));
    msg.remove(QRegularExpression("QVariant.+"));
    msg.remove(QRegularExpression("libGL.+"));
    msg.remove(QRegularExpression("Password.+"));
    msg.remove(QRegularExpression("gksu-run.+"));
    msg.remove(QRegularExpression("GConf Error:.+"));
    msg.remove(QRegularExpression(":: Do you want.+"));
    msg.remove(QRegularExpression("org\\.kde\\."));
    msg.remove(QRegularExpression("QCommandLineParser"));
    msg.remove(QRegularExpression("QCoreApplication.+"));
    msg.remove(QRegularExpression("Fontconfig warning.+"));
    msg.remove(QRegularExpression("reading configurations from.+"));
    msg.remove(QRegularExpression(".+annot load library.+"));
    msg.remove(QRegularExpression("pci id for fd \\d+.+"));

    //Gksu buggy strings
    msg.remove(QRegularExpression("you should recompile libgtop and dependent applications.+"));
    msg.remove(QRegularExpression("This libgtop was compiled on.+"));
    msg.remove(QRegularExpression("If you see strange problems caused by it.+"));
    msg.remove(QRegularExpression("LibGTop-Server.+"));
    msg.remove(QRegularExpression("received eof.+"));
    msg.remove(QRegularExpression("pid [0-9]+"));
    msg = msg.trimmed();

    QString order;
    int ini = msg.indexOf(QRegularExpression("\\(\\s{0,3}[0-9]{1,4}/[0-9]{1,4}\\) "));

    if (ini == 0)
    {
      int rp = msg.indexOf(")");
      if (rp == -1) return; //Guard!

      order = msg.left(rp+2);
      msg = msg.remove(0, rp+2);
    }

    if (!msg.isEmpty())
    {
      if (msg.contains(QRegularExpression("removing ")) && !m_textPrinted.contains(msg + " "))
      {
        //Does this package exist or is it a proccessOutput buggy string???
        QString pkgName = msg.mid(9).trimmed();

        if (pkgName.indexOf("...") != -1 || UnixCommand::isPackageInstalled(pkgName))
        {
          prepareTextToPrint("<b><font color=\"#E55451\">" + msg + "</font></b>"); //RED
        }
      }
      else
      {
        QString altMsg = msg;
        prepareTextToPrint(altMsg); //BLACK
      }
    }
  }
}

/*
 * Prepares a string parsed from pacman output to be printed by the UI
 */
void XBPSExec::prepareTextToPrint(QString str, TreatString ts, TreatURLLinks tl)
{
  if (m_debugMode) qDebug() << "_print: " << str;

  if (ts == ectn_DONT_TREAT_STRING)
  {
    emit textToPrintExt(str);
    return;
  }

  //If the msg waiting to being print is from curl status OR any other unwanted string...
  if ((str.contains(QRegularExpression("\\(\\d")) &&
       (!str.contains("target", Qt::CaseInsensitive)) &&
       (!str.contains("package", Qt::CaseInsensitive))) ||
      (str.contains(QRegularExpression("\\d\\)")) &&
       (!str.contains("target", Qt::CaseInsensitive)) &&
       (!str.contains("package", Qt::CaseInsensitive))) ||
      str.indexOf("Enter a selection", Qt::CaseInsensitive) == 0 ||
      str.indexOf("Proceed with", Qt::CaseInsensitive) == 0 ||
      str.indexOf("%") != -1 ||
      str.indexOf("---") != -1 ||
      str.indexOf("removed obsolete entry") != -1 ||
      str.indexOf("avg rate") != -1)
  {
    return;
  }

  //If the msg waiting to being print has not yet been printed...
  if(m_textPrinted.contains(str))
  {
    return;
  }

  QString newStr = str;

  if (newStr.contains(QRegularExpression("\\d+ downloaded, \\d+ installed, \\d+ updated, \\d+ configured, \\d+ removed")))
  {
    newStr = "<b>" + newStr + "</b>";
  }
  else if(newStr.contains(QRegularExpression("<font color")))
  {
    newStr += "<br>";
  }
  else
  {
    if(newStr.contains(QRegularExpression("removed")) ||
       newStr.contains(QRegularExpression("removing ")) ||
       newStr.contains(QRegularExpression("could not ")) ||
       newStr.contains(QRegularExpression("error")) ||
       newStr.contains(QRegularExpression("failed")) ||
       newStr.contains(QRegularExpression("is not synced")) ||
       newStr.contains(QRegularExpression("[Rr]emoving")) ||
       newStr.contains(QRegularExpression("[Dd]einstalling")) ||
       newStr.contains(QRegularExpression("could not be found")))
    {
      newStr = "<b><font color=\"#E55451\">" + newStr + "&nbsp;</font></b>"; //RED
    }
    else if(newStr.contains(QRegularExpression("reinstalled")) ||
            newStr.contains(QRegularExpression("installed")) ||
            newStr.contains(QRegularExpression("upgraded")) ||
            newStr.contains(QRegularExpression("updated")) ||
            newStr.contains(QRegularExpression("Verifying")) ||
            newStr.contains(QRegularExpression("Building")) ||
            newStr.contains(QRegularExpression("Checking")) ||
            newStr.contains(QRegularExpression("Configuring")) ||
            newStr.contains(QRegularExpression("Downloading")) ||
            newStr.contains(QRegularExpression("Reinstalling")) ||
            newStr.contains(QRegularExpression("Installing")) ||
            newStr.contains(QRegularExpression("Updating")) ||
            newStr.contains(QRegularExpression("Upgrading")) ||
            newStr.contains(QRegularExpression("Loading")) ||
            newStr.contains(QRegularExpression("Resolving")) ||
            newStr.contains(QRegularExpression("Extracting")) ||
            newStr.contains(QRegularExpression("Unpacking")) ||
            newStr.contains(QRegularExpression("Running")) ||
            newStr.contains(QRegularExpression("Looking")))
    {
      newStr = "<b><font color=\"#4BC413\">" + newStr + "</font></b>"; //GREEN
    }
    else if (newStr.contains(QRegularExpression("warning")) ||
             newStr.contains(QRegularExpression("downgrading")) ||
             newStr.contains(QRegularExpression("options changed")))
    {
      newStr = "<b><font color=\"#FF8040\">" + newStr + "</font></b>"; //ORANGE
    }
    else if (newStr.contains("-") &&
             (!newStr.contains(QRegularExpression("(is|are) up-to-date"))) &&
             (!newStr.contains(QRegularExpression("\\s"))))
    {
      newStr = "<b><font color=\"#FF8040\">" + newStr + "</font></b>"; //IT'S A PKGNAME!
    }
    /*else if (newMsg.contains(":") &&
               (!newMsg.contains(QRegularExpression("\\):"))) &&
               (!newMsg.contains(QRegularExpression(":$"))))
      {
        newMsg = "<b><font color=\"#FF8040\">" + newMsg + "</font></b>"; //IT'S A PKGNAME!
      }*/
  }

  if (newStr.contains("::"))
  {
    newStr = "<br><B>" + newStr + "</B><br><br>";
  }

  if (!newStr.contains(QRegularExpression("<br"))) //It was an else!
  {
    newStr += "<br>";
  }

  if (tl == ectn_TREAT_URL_LINK)
    newStr = Package::makeURLClickable(newStr);

  m_textPrinted.append(str);

  emit textToPrintExt(newStr);
}

/*
 * Whenever QProcess starts the pacman command...
 */
void XBPSExec::onStarted()
{
  //First we output the name of action we are starting to execute!
  if (m_commandExecuting == ectn_CLEAN_CACHE)
  {
    prepareTextToPrint("<b>" + StrConstants::getCleaningPackageCache() + "</b><br><br>", ectn_DONT_TREAT_STRING, ectn_DONT_TREAT_URL_LINK);
  }
  else if (m_commandExecuting == ectn_SYNC_DATABASE)
  {
    prepareTextToPrint("<b>" + StrConstants::getSyncDatabases() + "</b><br><br>", ectn_DONT_TREAT_STRING, ectn_DONT_TREAT_URL_LINK);
  }
  else if (m_commandExecuting == ectn_SYSTEM_UPGRADE || m_commandExecuting == ectn_RUN_SYSTEM_UPGRADE_IN_TERMINAL)
  {
    prepareTextToPrint("<b>" + StrConstants::getSystemUpgrade() + "</b><br><br>", ectn_DONT_TREAT_STRING, ectn_DONT_TREAT_URL_LINK);
  }
  else if (m_commandExecuting == ectn_REMOVE)
  {
    prepareTextToPrint("<b>" + StrConstants::getRemovingPackages() + "</b><br><br>", ectn_DONT_TREAT_STRING, ectn_DONT_TREAT_URL_LINK);
  }
  else if (m_commandExecuting == ectn_INSTALL)
  {
    prepareTextToPrint("<b>" + StrConstants::getInstallingPackages() + "</b><br><br>", ectn_DONT_TREAT_STRING, ectn_DONT_TREAT_URL_LINK);
  }
  else if (m_commandExecuting == ectn_REMOVE_INSTALL)
  {
    prepareTextToPrint("<b>" + StrConstants::getRemovingAndInstallingPackages() + "</b><br><br>", ectn_DONT_TREAT_STRING, ectn_DONT_TREAT_URL_LINK);
  }
  else if (m_commandExecuting == ectn_RUN_IN_TERMINAL)
  {
    prepareTextToPrint("<b>" + StrConstants::getRunningCommandInTerminal() + "</b><br><br>", ectn_DONT_TREAT_STRING, ectn_DONT_TREAT_URL_LINK);
  }

  QString output = m_unixCommand->readAllStandardOutput();
  output = output.trimmed();

  if (!output.isEmpty())
  {
    prepareTextToPrint(output);
  }

  emit started();
}

/*
 * Whenever QProcess' read output is retrieved...
 */
void XBPSExec::onReadOutput()
{
  if (WMHelper::getSUCommand().contains("kdesu"))
  {
    QString output = m_unixCommand->readAllStandardOutput();

    if (m_commandExecuting == ectn_SYNC_DATABASE &&
        output.contains("Usage: /usr/bin/kdesu [options] command"))
    {
      emit readOutput();
      return;
    }

    output = output.remove("Fontconfig warning: \"/etc/fonts/conf.d/50-user.conf\", line 14:");
    output = output.remove("reading configurations from ~/.fonts.conf is deprecated. please move it to /home/arnt/.config/fontconfig/fonts.conf manually");

    if (!output.trimmed().isEmpty())
    {
      splitOutputStrings(output);
    }
  }
  else if (WMHelper::getSUCommand().contains("gksu"))
  {
    QString output = m_unixCommand->readAllStandardOutput();
    output = output.trimmed();

    if(!output.isEmpty() &&
       output.indexOf(":: Synchronizing package databases...") == -1 &&
       output.indexOf(":: Starting full system upgrade...") == -1)
    {
      prepareTextToPrint(output);
    }
  }

  emit readOutput();
}

/*
 * Whenever QProcess' read error output is retrieved...
 */
void XBPSExec::onReadOutputError()
{
  QString msg = m_unixCommand->readAllStandardError();
  msg = msg.remove("Fontconfig warning: \"/etc/fonts/conf.d/50-user.conf\", line 14:");
  msg = msg.remove("reading configurations from ~/.fonts.conf is deprecated. please move it to /home/arnt/.config/fontconfig/fonts.conf manually");

  if (!msg.trimmed().isEmpty())
  {
    splitOutputStrings(msg);
  }

  emit readOutputError();
}

/*
 * Whenever QProcess finishes the pacman command...
 */
void XBPSExec::onFinished(int exitCode, QProcess::ExitStatus es)
{
  emit finished(exitCode, es);
}

// --------------------- DO METHODS ------------------------------------

/*
 * Cleans XBPS's package cache.
 */
void XBPSExec::doCleanCache()
{
  QString command = "xbps-remove -O";
  m_lastCommandList.clear();

  m_commandExecuting = ectn_CLEAN_CACHE;
  m_unixCommand->executeCommand(command);
}

/*
 * Calls pacman to install given packages and returns output to UI
 */
void XBPSExec::doInstall(const QString &listOfPackages)
{
  QString command = "xbps-install -y " + listOfPackages;

  m_lastCommandList.clear();
  m_lastCommandList.append("xbps-install " + listOfPackages + ";");
  m_lastCommandList.append("echo -e;");
  m_lastCommandList.append("read -n 1 -p \"" + StrConstants::getPressAnyKey() + "\"");

  m_commandExecuting = ectn_INSTALL;
  m_unixCommand->executeCommand(command);
}

/*
 * Calls pacman to install given packages inside a terminal
 */
void XBPSExec::doInstallInTerminal(const QString &listOfPackages)
{
  m_lastCommandList.clear();
  m_lastCommandList.append("xbps-install " + listOfPackages + ";");
  m_lastCommandList.append("echo -e;");
  m_lastCommandList.append("read -n 1 -p \"" + StrConstants::getPressAnyKey() + "\"");

  m_commandExecuting = ectn_RUN_IN_TERMINAL;
  m_unixCommand->runCommandInTerminal(m_lastCommandList);
}

/*
 * Calls pacman to install given LOCAL packages and returns output to UI
 */
void XBPSExec::doInstallLocal(const QString &listOfPackages)
{
  QString command = "pacman -U --force --noconfirm " + listOfPackages;

  m_lastCommandList.clear();
  m_lastCommandList.append("pacman -U --force " + listOfPackages + ";");
  m_lastCommandList.append("echo -e;");
  m_lastCommandList.append("read -n 1 -p \"" + StrConstants::getPressAnyKey() + "\"");

  m_commandExecuting = ectn_INSTALL;
  m_unixCommand->executeCommand(command);
}

/*
 * Calls pacman to install given LOCAL packages inside a terminal
 */
void XBPSExec::doInstallLocalInTerminal(const QString &listOfPackages)
{
  m_lastCommandList.clear();
  m_lastCommandList.append("pacman -U --force " + listOfPackages + ";");
  m_lastCommandList.append("echo -e;");
  m_lastCommandList.append("read -n 1 -p \"" + StrConstants::getPressAnyKey() + "\"");

  m_commandExecuting = ectn_RUN_IN_TERMINAL;
  m_unixCommand->runCommandInTerminal(m_lastCommandList);
}

/*
 * Calls pacman to remove given packages and returns output to UI
 */
void XBPSExec::doRemove(const QString &listOfPackages)
{
  QString command = "xbps-remove -R -y " + listOfPackages;

  m_lastCommandList.clear();
  m_lastCommandList.append("xbps-remove -R " + listOfPackages + ";");
  m_lastCommandList.append("echo -e;");
  m_lastCommandList.append("read -n 1 -p \"" + StrConstants::getPressAnyKey() + "\"");

  m_commandExecuting = ectn_REMOVE;
  m_unixCommand->executeCommand(command);
}

/*
 * Calls pacman to remove given packages inside a terminal
 */
void XBPSExec::doRemoveInTerminal(const QString &listOfPackages)
{
  m_lastCommandList.clear();
  m_lastCommandList.append("xbps-remove -R  " + listOfPackages + ";");
  m_lastCommandList.append("echo -e;");
  m_lastCommandList.append("read -n 1 -p \"" + StrConstants::getPressAnyKey() + "\"");

  m_commandExecuting = ectn_RUN_IN_TERMINAL;
  m_unixCommand->runCommandInTerminal(m_lastCommandList);
}

/*
 * Calls pacman to remove and install given packages and returns output to UI
 */
void XBPSExec::doRemoveAndInstall(const QString &listOfPackagestoRemove, const QString &listOfPackagestoInstall)
{
  QString command = "xbps-remove -R -y " + listOfPackagestoRemove +
      "; xbps-install " + listOfPackagestoInstall;

  m_lastCommandList.clear();
  m_lastCommandList.append("xbps-remove -R " + listOfPackagestoRemove + ";");
  m_lastCommandList.append("xbps-install  " + listOfPackagestoInstall + ";");
  m_lastCommandList.append("echo -e;");
  m_lastCommandList.append("read -n 1 -p \"" + StrConstants::getPressAnyKey() + "\"");

  m_commandExecuting = ectn_REMOVE_INSTALL;
  m_unixCommand->executeCommand(command);
}

/*
 * Calls pacman to remove and install given packages inside a terminal
 */
void XBPSExec::doRemoveAndInstallInTerminal(const QString &listOfPackagestoRemove, const QString &listOfPackagestoInstall)
{
  m_lastCommandList.clear();
  m_lastCommandList.append("xbps-remove -R  " + listOfPackagestoRemove + ";");
  m_lastCommandList.append("xbps-install  " + listOfPackagestoInstall + ";");
  m_lastCommandList.append("echo -e;");
  m_lastCommandList.append("read -n 1 -p \"" + StrConstants::getPressAnyKey() + "\"");

  m_commandExecuting = ectn_RUN_IN_TERMINAL;
  m_unixCommand->runCommandInTerminal(m_lastCommandList);
}

/*
 * Calls pacman to upgrade the entire system and returns output to UI
 */
void XBPSExec::doSystemUpgrade()
{
  QString command = "xbps-install -u -y";

  m_lastCommandList.clear();
  m_lastCommandList.append("xbps-install -u;");
  m_lastCommandList.append("echo -e;");
  m_lastCommandList.append("read -n 1 -p \"" + StrConstants::getPressAnyKey() + "\"");

  m_commandExecuting = ectn_SYSTEM_UPGRADE;
  m_unixCommand->executeCommand(command);
}

/*
 * Calls pacman to upgrade the entire system inside a terminal
 */
void XBPSExec::doSystemUpgradeInTerminal()
{
  m_lastCommandList.clear();
  m_lastCommandList.append("pacman -Su;");
  m_lastCommandList.append("echo -e;");
  m_lastCommandList.append("read -n 1 -p \"" + StrConstants::getPressAnyKey() + "\"");

  m_commandExecuting = ectn_RUN_SYSTEM_UPGRADE_IN_TERMINAL;
  m_unixCommand->runCommandInTerminal(m_lastCommandList);
}

/*
 * Calls pacman to sync databases and returns output to UI
 */
void XBPSExec::doSyncDatabase()
{
  QString command;

  if (UnixCommand::isRootRunning())
    command = "xbps-install -Sy";
  else
    command = "xbps-install -Syy";

  if (UnixCommand::hasTheExecutable("pkgfile") && !UnixCommand::isRootRunning())
    command += "; pkgfile -u";

  m_commandExecuting = ectn_SYNC_DATABASE;
  m_unixCommand->executeCommand(command);
}

/*
 * Runs latest command inside a terminal (probably due to some previous error)
 */
void XBPSExec::runLastestCommandInTerminal()
{
  m_commandExecuting = ectn_RUN_IN_TERMINAL;
  m_unixCommand->runCommandInTerminal(m_lastCommandList);
}
