#pragma once

#include <QHash>
#include <QStringList>
#include <QtGlobal>
#include <QtMath>

struct PhoneBattery {
    enum State { Unknown = 1, Charging, Discharging, NotCharging, Full };

    int percent = -1;
    State state = Unknown;
    double watts = -1.0;
    double voltage = -1.0;
    double currentAmps = -1.0;
    double temperatureC = qQNaN();
    QStringList powerSources;

    static PhoneBattery fromAdb(const QString &output)
    {
        QHash<QString, QString> fields;
        for (const auto &row : output.split(QLatin1Char('\n'))) {
            const int split = row.indexOf(row.startsWith(QLatin1String("POWER_SUPPLY_")) ? '=' : ':');
            if (split > 0) fields.insert(row.left(split).trimmed(), row.mid(split + 1).trimmed());
        }

        PhoneBattery reading;
        if (fields.value(QStringLiteral("present")) == QLatin1String("false")) return reading;
        for (const auto &source : {QStringLiteral("AC"), QStringLiteral("USB"),
                                   QStringLiteral("Wireless"), QStringLiteral("Dock")}) {
            if (fields.value(source + QStringLiteral(" powered")) == QLatin1String("true")) {
                reading.powerSources.append(source);
            }
        }
        bool hasTemperature = false;
        const int temperature = fields.value(QStringLiteral("temperature"),
            fields.value(QStringLiteral("POWER_SUPPLY_TEMP"))).toInt(&hasTemperature);
        if (hasTemperature && temperature >= -400 && temperature <= 1000) reading.temperatureC = temperature / 10.0;
        bool hasLevel = false;
        const int level = fields.value(QStringLiteral("level")).toInt(&hasLevel);
        const int scale = fields.value(QStringLiteral("scale"), QStringLiteral("100")).toInt();
        if (hasLevel && scale > 0 && level >= 0 && level <= scale) {
            reading.percent = qRound(100.0 * level / scale);
        } else {
            bool hasCapacity = false;
            const int capacity = fields.value(QStringLiteral("POWER_SUPPLY_CAPACITY")).toInt(&hasCapacity);
            if (hasCapacity && capacity >= 0 && capacity <= 100) reading.percent = capacity;
        }

        const int status = fields.value(QStringLiteral("status")).toInt();
        if (status >= Charging && status <= Full) {
            reading.state = static_cast<State>(status);
        } else {
            const auto value = fields.value(QStringLiteral("POWER_SUPPLY_STATUS"));
            if (value == QLatin1String("Charging")) reading.state = Charging;
            else if (value == QLatin1String("Discharging")) reading.state = Discharging;
            else if (value == QLatin1String("Not charging")) reading.state = NotCharging;
            else if (value == QLatin1String("Full")) reading.state = Full;
        }

        // The battery supply reports current and voltage in micro-units.
        // The framework voltage is in millivolts. Charger ratings are not measurements.
        double voltage = fields.value(QStringLiteral("POWER_SUPPLY_VOLTAGE_NOW")).toDouble() / 1000000.0;
        if (!(voltage >= 2 && voltage <= 20)) {
            voltage = fields.value(QStringLiteral("voltage")).toDouble() / 1000.0;
        }
        if (voltage >= 2 && voltage <= 20) reading.voltage = voltage;
        bool hasCurrent = false;
        const double current = qAbs(fields.value(QStringLiteral("POWER_SUPPLY_CURRENT_NOW")).toDouble(&hasCurrent)) / 1000000.0;
        if (hasCurrent && current <= 20) reading.currentAmps = current;
        if (reading.currentAmps >= 0 && reading.voltage >= 0 &&
            (reading.state == Charging || reading.state == Discharging)) {
            reading.watts = reading.voltage * reading.currentAmps;
        }
        return reading;
    }
};
