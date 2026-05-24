#pragma once
#include <QDialog>
#include "epubreader.h"

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QLabel;

class SearchDialog : public QDialog {
    Q_OBJECT
public:
    explicit SearchDialog(EpubReader* reader, QWidget* parent = nullptr);
    void setResults(const QString& query, const QList<EpubReader::SearchResult>& results, int currentIndex = -1);

signals:
    void resultSelected(int resultIndex, int chapterIndex, const QString& href,
                        const QString& query, int occurrenceIndex);

private:
    void performSearch();
    void onItemActivated(QListWidgetItem* item);

    EpubReader*  m_reader;
    QLineEdit*   m_searchEdit;
    QListWidget* m_resultList;
    QLabel*      m_statusLabel;
    QList<EpubReader::SearchResult> m_results;
};
