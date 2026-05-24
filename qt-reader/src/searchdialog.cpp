#include "searchdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>

SearchDialog::SearchDialog(EpubReader* reader, QWidget* parent)
    : QDialog(parent), m_reader(reader)
{
    setWindowTitle(tr("全文検索"));
    setMinimumSize(560, 420);

    auto* layout = new QVBoxLayout(this);

    auto* row = new QHBoxLayout;
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("検索キーワードを入力…"));
    auto* btn = new QPushButton(tr("検索"), this);
    row->addWidget(m_searchEdit);
    row->addWidget(btn);
    layout->addLayout(row);

    m_resultList = new QListWidget(this);
    layout->addWidget(m_resultList);

    m_statusLabel = new QLabel(this);
    layout->addWidget(m_statusLabel);

    connect(btn,          &QPushButton::clicked,
            this,         &SearchDialog::performSearch);
    connect(m_searchEdit, &QLineEdit::returnPressed,
            this,         &SearchDialog::performSearch);
    connect(m_resultList, &QListWidget::itemActivated,
            this,         &SearchDialog::onItemActivated);
}

void SearchDialog::setResults(const QString& query, const QList<EpubReader::SearchResult>& results, int currentIndex) {
    m_searchEdit->setText(query);
    m_results = results;
    m_resultList->clear();
    for (const auto& r : m_results) {
        QString line = QString("[%1]  %2").arg(r.chapterTitle, r.context);
        m_resultList->addItem(line);
    }
    if (currentIndex >= 0 && currentIndex < m_resultList->count())
        m_resultList->setCurrentRow(currentIndex);
    m_statusLabel->setText(tr("%1 件見つかりました。").arg(m_results.size()));
}

void SearchDialog::performSearch() {
    m_resultList->clear();
    m_results.clear();

    QString q = m_searchEdit->text().trimmed();
    if (q.isEmpty() || !m_reader) return;

    m_results = m_reader->search(q);
    for (const auto& r : m_results) {
        QString line = QString("[%1]  %2").arg(r.chapterTitle, r.context);
        m_resultList->addItem(line);
    }

    if (m_results.isEmpty())
        m_statusLabel->setText(tr("「%1」は見つかりませんでした。").arg(q));
    else
        m_statusLabel->setText(tr("%1 件見つかりました。").arg(m_results.size()));
}

void SearchDialog::onItemActivated(QListWidgetItem* item) {
    int row = m_resultList->row(item);
    if (row >= 0 && row < m_results.size()) {
        emit resultSelected(row,
                            m_results[row].chapterIndex,
                            m_results[row].href,
                            m_searchEdit->text().trimmed(),
                            m_results[row].occurrenceIndex);
    }
}
