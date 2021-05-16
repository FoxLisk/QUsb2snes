#include <QLoggingCategory>

#include "retroarchhost.h"

Q_LOGGING_CATEGORY(log_retroarchhost, "RetroArcHost")
#define sDebug() qCDebug(log_retroarchhost) << m_name


RetroArchHost::RetroArchHost(QString name, QObject *parent) : QObject(parent)
{
    m_name = name;
    m_port = 55355;
    readRamHasRomAccess = false;
    readMemory = false;
    m_connected = false;
    connect(&socket, &QUdpSocket::connected, this, &RetroArchHost::connected);
    connect(&socket, &QUdpSocket::connected, this, [=](){m_connected = true;});
    connect(&socket, &QUdpSocket::readyRead, this, &RetroArchHost::onReadyRead);
    connect(&socket, &QUdpSocket::disconnected, this, &RetroArchHost::disconnected);
    connect(&socket, &QUdpSocket::disconnected, this, [=](){m_connected = false;});
#if QT_VERSION >= QT_VERSION_CHECK(5,15,0)
    connect(&socket, &QUdpSocket::errorOccurred, this, &RetroArchHost::errorOccured);
    connect(&socket, &QUdpSocket::errorOccurred, this, [this]{sDebug() << socket.error() << socket.errorString(); m_connected = false;});
#else
    connect(&socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error), this, &RetroArchHost::errorOccured);
#endif
    state = None;
}

void RetroArchHost::connectToHost()
{
    sDebug() << "Connecting to " << m_address << m_port;
    socket.connectToHost(m_address, m_port);
}

void RetroArchHost::setHostAddress(QHostAddress addr, quint16 port)
{
    m_address = addr;
    m_port = port;
}

qint64 RetroArchHost::getMemory(unsigned int address, unsigned int size)
{
    int raAddress = translateAddress(address);
    if (raAddress == -1)
        return -1;
    QByteArray data = (readMemory ? "READ_CORE_MEMORY " : "READ_CORE_RAM ") + QByteArray::number(raAddress, 16) + " " + QByteArray::number(size);
    getMemorySize = size;
    sDebug() << ">>" << data;
    socket.write(data);
    state = GetMemory;
    return ++id;
}

qint64 RetroArchHost::getInfos()
{
    sDebug() << m_name << "Doing info";
    socket.write("VERSION");
    state = ReqInfoVersion;
    return ++id;
}

QVersionNumber RetroArchHost::version() const
{
    return m_version;
}

QByteArray RetroArchHost::getMemoryData() const
{
    return getMemoryDatas;
}

QString RetroArchHost::name() const
{
    return  m_name;
}

QString RetroArchHost::gameTitle() const
{
    return m_gameTile;
}

bool RetroArchHost::hasRomAccess() const
{
    if (readMemory)
        return false;
    return readRamHasRomAccess;
}

QHostAddress RetroArchHost::address() const
{
    return m_address;
}

bool RetroArchHost::isConnected() const
{
    return m_connected;
}

void RetroArchHost::setInfoFromRomHeader(QByteArray data)
{
    struct rom_infos* rInfos = get_rom_info(data);
    sDebug() << "From header : " << rInfos->title << rInfos->type;
    if (!readMemory)
        m_gameTile = rInfos->title;
    romType = rInfos->type;
    free(rInfos);
}


void RetroArchHost::makeInfoFail(QString error)
{
    state = None;
    sDebug() << "Info error " << error;
    m_lastInfoError = error;
    emit infoFailed(id);
}

void RetroArchHost::onReadyRead()
{
    QByteArray data = socket.readAll();
    sDebug() << data;
    switch(state)
    {
        case GetMemory:
        {
            QList<QByteArray> tList = data.trimmed().split(' ');
            tList = tList.mid(2);
            getMemoryDatas = QByteArray::fromHex(tList.join());
            emit getMemoryDone(id);
            break;
        }

        /*
         * Info Stuff
        */
        case ReqInfoVersion:
        {
            QString raVersion = data.trimmed();
            m_version = QVersionNumber::fromString(raVersion);
            if (raVersion == "")
            {
                makeInfoFail("RetroArch did not reply to version");
                break;
            }
            sDebug() << "Version : " << m_version;
            readMemory = m_version >= QVersionNumber(1,9,0);
            if (readMemory)
            {
                socket.write("GET_STATUS");
                state = ReqInfoStatus;
            } else {
                socket.write("READ_CORE_RAM 0 1");
                state = ReqInfoRRAMZero;
            }
            break;
        }
        // These are the step for the newest RA with the READ_MEMORY api
        case ReqInfoStatus:
        {
            // GET_STATUS PAUSED super_nes,Secret of Evermore,crc32=5756f698
            QRegExp statusExp("^GET_STATUS (\\w+)(\\s.+)?");
            if (statusExp.indexIn(data))
            {
                makeInfoFail("RetroArch did not return a proper formated status reply");
                break;
            }
            QString status = statusExp.cap(1);
            QString infos = statusExp.cap(2).trimmed();
            QString plateform;
            QString game;

            sDebug() << status << infos;
            if (infos.isEmpty() == false)
            {
                plateform = infos.split(',').at(0);
                game = infos.split(',').at(1);
            }
            if (status == "CONTENTLESS" || plateform != "super_nes")
            {
                makeInfoFail("RetroArch not running a super nintendo game");
                break;
            }
            m_gameTile = game;
            state = ReqInfoRMemoryWRAM;
            socket.write("READ_CORE_MEMORY 7E0000 1");
            break;
        }
        case ReqInfoRMemoryWRAM:
        {
            if (data.split(' ').at(2) == "-1")
            {
                makeInfoFail("Could not read WRAM");
                break;
            }
            state = ReqInfoRMemoryHiRomData;
            socket.write("READ_CORE_MEMORY 40FFC0 32");

            break;
        }
        case ReqInfoRMemoryHiRomData:
        case ReqInfoRMemoryLoRomData:
        {
            QList<QByteArray> tList = data.trimmed().split(' ');
            tList.removeFirst();
            tList.removeFirst();
            if (tList.at(0) == "-1")
            {
                if (state == ReqInfoRMemoryHiRomData)
                {
                    socket.write("READ_CORE_MEMORY FFC0 32");
                    state = ReqInfoRMemoryLoRomData;
                    break;
                } else {
                    // This should actually still be valid, since you can read wram
                    // but sram will be 'impossible' without the rom mapping
                    makeInfoFail("Can't get ROM info");
                    break;
                }
            }
            setInfoFromRomHeader(QByteArray::fromHex(tList.join()));
            emit infoDone(id);
            break;
        }
        // This is the part for the old RA Version
        case ReqInfoRRAMZero:
        {
            if (data == "READ_CORE_RAM 0 -1\n")
            {
                makeInfoFail("No game is running or core does not support memory read.");
                break;
            }
            socket.write("READ_CORE_RAM 40FFC0 32");
            state = ReqInfoRRAMHiRomData;
            break;
        }
        case ReqInfoRRAMHiRomData:
        {
            QList<QByteArray> tList = data.trimmed().split(' ');
            if (tList.at(2) == "-1")
            {
                readRamHasRomAccess = false;
            } else {
                tList.removeFirst();
                tList.removeFirst();
                setInfoFromRomHeader(QByteArray::fromHex(tList.join()));
                readRamHasRomAccess = true;
            }
            emit infoDone(id);
            break;
        }
        default:
            break;
    }
}

int RetroArchHost::translateAddress(unsigned int address)
{
    int addr = static_cast<int>(address);
    if (readMemory)
    {
        // ROM ACCESS is like whatever it seems let's not bother with it
        if (addr < 0xE00000)
            return -1;//return rommapping_pc_to_snes(address, romType, false);
        if (addr >= 0xF50000 && addr <= 0xF70000)
            return 0x7E0000 + addr - 0xF50000;
        if (addr >= 0xE00000 && addr < 0xF70000)
            return rommapping_sram_pc_to_snes(address - 0xE00000, romType, false);
        return -1;
    } else {
        // Rom access
        if (addr < 0xE00000)
        {
            if (!hasRomAccess())
                return -1;
            if (romType == LoROM)
                return 0x800000 + (addr + (0x8000 * ((addr + 0x8000) / 0x8000)));
            if (romType == HiROM)
            {
                if (addr < 0x400000)
                    return addr + 0xC00000;
                else //exhirom
                    return addr + 0x400000;
            }
        }
        // WRAM access is pretty simple
        if (addr >= 0xF50000 && addr <= 0xF70000)
        {
            if (hasRomAccess()) {
                return addr - 0xF50000 + 0x7E0000;
            } else {
                return addr - 0xF50000;
            }
        }
        if (addr >= 0xE00000 && addr < 0xF70000)
        {
            if (!hasRomAccess())
                return addr - 0xE00000 + 0x20000;
            if (romType == LoROM)
                return addr - 0xE00000 + 0x700000;
            return lorom_sram_pc_to_snes(address - 0xE00000);
        }
        return -1;
    }
}
