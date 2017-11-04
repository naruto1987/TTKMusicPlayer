#include "musicdownloadquerykwthread.h"
#include "musicdownloadqueryyytthread.h"
#include "musicsemaphoreloop.h"
#include "musicnumberutils.h"
#include "musiccoreutils.h"
#include "musictime.h"
#///QJson import
#include "qjson/parser.h"

MusicDownLoadQueryKWThread::MusicDownLoadQueryKWThread(QObject *parent)
    : MusicDownLoadQueryThreadAbstract(parent)
{
    m_queryServer = "Kuwo";
}

QString MusicDownLoadQueryKWThread::getClassName()
{
    return staticMetaObject.className();
}

void MusicDownLoadQueryKWThread::startToSearch(QueryType type, const QString &text)
{
    if(!m_manager)
    {
        return;
    }

    M_LOGGER_INFO(QString("%1 startToSearch %2").arg(getClassName()).arg(text));
    m_searchText = text.trimmed();
    m_currentType = type;
    QUrl musicUrl = MusicUtils::Algorithm::mdII(KW_SONG_SEARCH_URL, false).arg(text).arg(0).arg(50);
    deleteAll();

    QNetworkRequest request;
    request.setUrl(musicUrl);
    request.setRawHeader("Content-Type", "application/x-www-form-urlencoded");
#ifndef QT_NO_SSL
    QSslConfiguration sslConfig = request.sslConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(sslConfig);
#endif
    m_reply = m_manager->get(request);
    connect(m_reply, SIGNAL(finished()), SLOT(downLoadFinished()));
    connect(m_reply, SIGNAL(error(QNetworkReply::NetworkError)), SLOT(replyError(QNetworkReply::NetworkError)));
}

void MusicDownLoadQueryKWThread::downLoadFinished()
{
    if(!m_reply || !m_manager)
    {
        deleteAll();
        return;
    }

    M_LOGGER_INFO(QString("%1 downLoadFinished").arg(getClassName()));
    emit clearAllItems();      ///Clear origin items
    m_musicSongInfos.clear();  ///Empty the last search to songsInfo

    if(m_reply->error() == QNetworkReply::NoError)
    {
        QByteArray bytes = m_reply->readAll();///Get all the data obtained by request

        QJson::Parser parser;
        bool ok;
        QVariant data = parser.parse(bytes.replace("'", "\""), &ok);

        if(ok)
        {
            QVariantMap value = data.toMap();
            if(value.contains("abslist"))
            {
                QVariantList datas = value["abslist"].toList();
                foreach(const QVariant &var, datas)
                {
                    if(var.isNull())
                    {
                        continue;
                    }

                    value = var.toMap();
                    MusicObject::MusicSongInformation musicInfo;
                    musicInfo.m_singerName = value["ARTIST"].toString();
                    musicInfo.m_songName = value["SONGNAME"].toString();
                    musicInfo.m_timeLength = MusicTime::msecTime2LabelJustified(value["DURATION"].toString().toInt()*1000);

                    if(m_currentType != MovieQuery)
                    {
                        musicInfo.m_songId = value["MUSICRID"].toString().replace("MUSIC_", "");
                        musicInfo.m_artistId = value["ARTISTID"].toString();
                        musicInfo.m_albumId = value["ALBUMID"].toString();
                        if(!m_querySimplify)
                        {
                            musicInfo.m_albumName = value["ALBUM"].toString();

                            if(!m_manager || m_stateCode != MusicNetworkAbstract::Init) return;
                            readFromMusicSongPic(&musicInfo, musicInfo.m_songId);
                            if(!m_manager || m_stateCode != MusicNetworkAbstract::Init) return;
                            musicInfo.m_lrcUrl = MusicUtils::Algorithm::mdII(KW_SONG_INFO_URL, false).arg(musicInfo.m_songId);
                            ///music normal songs urls
                            readFromMusicSongAttribute(&musicInfo, value["FORMATS"].toString(), m_searchQuality, m_queryAllRecords);
                            if(!m_manager || m_stateCode != MusicNetworkAbstract::Init) return;

                            if(musicInfo.m_songAttrs.isEmpty())
                            {
                                continue;
                            }
                            ////////////////////////////////////////////////////////////
                            for(int i=0; i<musicInfo.m_songAttrs.count(); ++i)
                            {
                                MusicObject::MusicSongAttribute *attr = &musicInfo.m_songAttrs[i];
                                if(!m_manager || m_stateCode != MusicNetworkAbstract::Init) return;
                                attr->m_size = MusicUtils::Number::size2Label(getUrlFileSize(attr->m_url));
                                if(!m_manager || m_stateCode != MusicNetworkAbstract::Init) return;
                            }
                            ////////////////////////////////////////////////////////////
                            MusicSearchedItem item;
                            item.m_songName = musicInfo.m_songName;
                            item.m_singerName = musicInfo.m_singerName;
                            item.m_albumName = musicInfo.m_albumName;
                            item.m_time = musicInfo.m_timeLength;
                            item.m_type = mapQueryServerString();
                            emit createSearchedItems(item);
                        }
                        m_musicSongInfos << musicInfo;
                    }
                    else
                    {
                        //mv
                        musicInfo.m_songId = value["MUSICRID"].toString();
                        if(!m_manager || m_stateCode != MusicNetworkAbstract::Init) return;
                        readFromMusicMVInfoAttribute(&musicInfo);
                        if(!m_manager || m_stateCode != MusicNetworkAbstract::Init) return;

                        if(musicInfo.m_songAttrs.isEmpty())
                        {
                          continue;
                        }

                        MusicSearchedItem item;
                        item.m_songName = musicInfo.m_songName;
                        item.m_singerName = musicInfo.m_singerName;
                        item.m_time = musicInfo.m_timeLength;
                        item.m_type = mapQueryServerString();
                        emit createSearchedItems(item);
                        m_musicSongInfos << musicInfo;
                    }
                }
            }
        }
    }

    ///extra yyt movie
    if(m_queryExtraMovie && m_currentType == MovieQuery)
    {
        MusicSemaphoreLoop loop;
        MusicDownLoadQueryYYTThread *yyt = new MusicDownLoadQueryYYTThread(this);
        connect(yyt, SIGNAL(createSearchedItems(MusicSearchedItem)), SIGNAL(createSearchedItems(MusicSearchedItem)));
        connect(yyt, SIGNAL(downLoadDataChanged(QString)), &loop, SLOT(quit()));
        yyt->startToSearch(MusicDownLoadQueryYYTThread::MovieQuery, m_searchText);
        loop.exec();
        m_musicSongInfos << yyt->getMusicSongInfos();
    }

    emit downLoadDataChanged(QString());
    deleteAll();
    M_LOGGER_INFO(QString("%1 downLoadFinished deleteAll").arg(getClassName()));
}

void MusicDownLoadQueryKWThread::readFromMusicMVInfoAttribute(MusicObject::MusicSongInformation *info)
{
    if(info->m_songId.isEmpty() || !m_manager)
    {
        return;
    }

    QUrl musicUrl = MusicUtils::Algorithm::mdII(KW_MV_ATTR_URL, false).arg(info->m_songId);

    QNetworkRequest request;
    request.setUrl(musicUrl);
    request.setRawHeader("Content-Type", "application/x-www-form-urlencoded");
#ifndef QT_NO_SSL
    QSslConfiguration sslConfig = request.sslConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(sslConfig);
#endif
    MusicSemaphoreLoop loop;
    QNetworkReply *reply = m_manager->get( request );
    QObject::connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
    QObject::connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), &loop, SLOT(quit()));
    loop.exec();

    if(!reply || reply->error() != QNetworkReply::NoError)
    {
        return;
    }

    QByteArray data = reply->readAll();
    if(!data.contains("res not found"))
    {
        MusicObject::MusicSongAttribute attr;
        attr.m_url = data;
        attr.m_bitrate = MB_500;
        attr.m_format = MusicUtils::Core::fileSuffix(attr.m_url);
        if(!m_manager || m_stateCode != MusicNetworkAbstract::Init) return;
        attr.m_size = MusicUtils::Number::size2Label(getUrlFileSize(attr.m_url));
        if(!m_manager || m_stateCode != MusicNetworkAbstract::Init) return;
        info->m_songAttrs.append(attr);
    }
}
