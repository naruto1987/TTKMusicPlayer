#include "musicbackgroundconfigmanager.h"

MusicSkinConfigManager::MusicSkinConfigManager(QObject *parent)
    : TTKXmlDocument(parent)
{

}

void MusicSkinConfigManager::readBuffer(MusicSkinItem &item)
{
    item.m_name = readXmlAttributeByTagName("name");
    item.m_useCount = readXmlAttributeByTagName("useCount").toInt();
}

void MusicSkinConfigManager::writeBuffer(const MusicSkinItem &item, const QString &path)
{
    if(!toFile(path))
    {
        return;
    }

    createProcessingInstruction();
    QDomElement rootDom = createRoot("TTKSkin");

    writeDomElement(rootDom, "creator", {"value", TTK_APP_NAME});
    writeDomElement(rootDom, "name", {"value", item.m_name});
    writeDomElement(rootDom, "useCount", {"value", item.m_useCount});

    QTextStream out(m_file);
    m_document->save(out, 4);
}
