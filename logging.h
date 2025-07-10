#pragma once

#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/expressions/formatters/date_time.hpp>
#include <boost/log/core.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/core/null_deleter.hpp>
#include <ostream>
#include <string>

inline void init_logging(const std::string &tag) {
    namespace logging = boost::log;
    namespace sinks = boost::log::sinks;
    namespace expr = boost::log::expressions;

    using text_sink = sinks::asynchronous_sink<sinks::text_ostream_backend>;
    auto sink = boost::make_shared<text_sink>();
    sink->locked_backend()->add_stream(
        boost::shared_ptr<std::ostream>(&std::clog, boost::null_deleter{}));
    sink->locked_backend()->auto_flush(true);
    sink->set_formatter(
        expr::stream
            << expr::format_date_time<boost::posix_time::ptime>(
                   "TimeStamp", "%Y-%m-%d %H:%M:%S")
            << " [" << expr::attr<std::string>("Tag") << "] "
            << expr::smessage);

    logging::core::get()->add_sink(sink);
    logging::add_common_attributes();
    logging::core::get()->add_global_attribute(
        "Tag", logging::attributes::constant<std::string>(tag));
}
