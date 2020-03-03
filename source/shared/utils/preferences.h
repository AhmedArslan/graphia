#ifndef PREFERENCES_H
#define PREFERENCES_H

#include "singleton.h"

#include <QObject>
#include <QSettings>
#include <QVariant>
#include <QString>
#include <QQmlParserStatus>

#include <map>

class Preferences : public QObject, public Singleton<Preferences>
{
    Q_OBJECT

private:
    QSettings _settings;
    std::map<QString, QVariant> _defaultValue;
    std::map<QString, QVariant> _minimumValue;
    std::map<QString, QVariant> _maximumValue;

public:
    void define(const QString& key, const QVariant& defaultValue = QVariant(),
                const QVariant& minimumValue = QVariant(), const QVariant& maximumValue = QVariant());

    QVariant get(const QString& key);

    QVariant minimum(const QString& key) const;
    QVariant maximum(const QString& key) const;

    void set(const QString& key, QVariant value, bool notify = true);
    void reset(const QString& key);

    bool exists(const QString& key);

signals:
    void preferenceChanged(const QString& key, const QVariant& value);
    void minimumChanged(const QString& key, const QVariant& value);
    void maximumChanged(const QString& key, const QVariant& value);
};

class QmlPreferences : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(QString section READ section WRITE setSection NOTIFY sectionChanged)

public:
    explicit QmlPreferences(QObject* parent = nullptr);
    ~QmlPreferences() override;

    QString section() const;
    void setSection(const QString& section);

    Q_INVOKABLE void reset(const QString& key);

private:
    bool _initialised = false;
    QString _section;
    std::map<QString, QVariant> _pendingPreferenceChanges;
    int _timerId = 0;

    void timerEvent(QTimerEvent *) override;

    void classBegin() override {}
    void componentComplete() override;

    QString preferenceNameByPropertyName(const QString& propertyName);
    QMetaProperty propertyByName(const QString& propertyName) const;

    QMetaProperty valuePropertyFrom(const QString& preferenceName);
    QMetaProperty minimumPropertyFrom(const QString& preferenceName);
    QMetaProperty maximumPropertyFrom(const QString& preferenceName);

    void setProperty(QMetaProperty property, const QVariant& value);

    void load();
    void save(bool notify = true);
    void flush(bool notify = true);

    Q_DISABLE_COPY(QmlPreferences)

private slots:
    void onPreferenceChanged(const QString& key, const QVariant& value);
    void onMinimumChanged(const QString& key, const QVariant& value);
    void onMaximumChanged(const QString& key, const QVariant& value);

    void onPropertyChanged();

signals:
    void sectionChanged();
};

namespace u
{
    template<typename... Args> void definePref(Args&&... args)  { return S(Preferences)->define(std::forward<Args>(args)...); }
    template<typename... Args> QVariant pref(Args&&... args)    { return S(Preferences)->get(std::forward<Args>(args)...); }
    template<typename... Args> QVariant minPref(Args&&... args) { return S(Preferences)->minimum(std::forward<Args>(args)...); }
    template<typename... Args> QVariant maxPref(Args&&... args) { return S(Preferences)->maximum(std::forward<Args>(args)...); }
    template<typename... Args> void setPref(Args&&... args)     { return S(Preferences)->set(std::forward<Args>(args)...); }
    template<typename... Args> void resetPref(Args&&... args)   { return S(Preferences)->reset(std::forward<Args>(args)...); }
    template<typename... Args> bool prefExists(Args&&... args)  { return S(Preferences)->exists(std::forward<Args>(args)...); }
} // namespace u

#endif // PREFERENCES_H

