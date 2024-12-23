#include "cypress.h"

#include <yt/yt/server/lib/signature/key_info.h>
#include <yt/yt/server/lib/signature/private.h>

#include <yt/yt/ytlib/api/native/client.h>

#include <yt/yt/core/ypath/helpers.h>

#include <yt/yt/core/ytree/convert.h>

namespace NYT::NSignature {

////////////////////////////////////////////////////////////////////////////////

using namespace NApi;
using namespace NConcurrency;
using namespace NObjectClient;
using namespace NYson;
using namespace NYPath;
using namespace NYTree;
using namespace NThreading;

YT_DEFINE_GLOBAL(TYPath, DefaultPath, "//sys/public_keys/by_owner");

////////////////////////////////////////////////////////////////////////////////

void TCypressKeyReaderConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("path", &TThis::Path)
        .Default(DefaultPath())
        .NonEmpty();
}

void TCypressKeyWriterConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("path", &TThis::Path)
        .Default(DefaultPath())
        .NonEmpty();

    registrar.Parameter("owner", &TThis::Owner)
        .CheckThat([] (const TOwnerId& owner) { return !owner.Underlying().empty(); });

    registrar.Parameter("key_deletion_delay", &TThis::KeyDeletionDelay)
        .Default(TDuration::Days(1))
        .GreaterThanOrEqual(TDuration::Zero());

    // TODO(pavook) implement.
    registrar.Parameter("max_key_count", &TThis::MaxKeyCount)
        .Default(100)
        .GreaterThan(0);
}

////////////////////////////////////////////////////////////////////////////////

TCypressKeyReader::TCypressKeyReader(TCypressKeyReaderConfigPtr config, IClientPtr client)
    : Config_(std::move(config))
    , Client_(std::move(client))
{ }

TFuture<TKeyInfoPtr> TCypressKeyReader::FindKey(const TOwnerId& owner, const TKeyId& key)
{
    auto keyNodePath = GetCypressKeyPath(Config_->Path, owner, key);
    auto res = Client_->GetNode(keyNodePath);

    return res.Apply(BIND([](const TYsonString& str) {
        return ConvertTo<TKeyInfoPtr>(str);
    }));
}

////////////////////////////////////////////////////////////////////////////////

TCypressKeyWriter::TCypressKeyWriter(TCypressKeyWriterConfigPtr config, IClientPtr client)
    : Config_(std::move(config))
    , Client_(std::move(client))
{ }

TFuture<void> TCypressKeyWriter::Initialize()
{
    TCreateNodeOptions options;
    options.IgnoreExisting = true;

    Initialization_ = Client_->CreateNode(
        YPathJoin(Config_->Path, ToYPathLiteral(Config_->Owner)),
        EObjectType::MapNode,
        std::move(options)).AsVoid();

    YT_LOG_INFO("Initialized Cypress key writer");

    return Initialization_;
}

TOwnerId TCypressKeyWriter::GetOwner()
{
    return Config_->Owner;
}

TFuture<void> TCypressKeyWriter::RegisterKey(const TKeyInfoPtr& keyInfo)
{
    YT_ASSERT(keyInfo);

    return Initialization_.Apply(BIND([this, keyInfo, this_ = MakeStrong(this)] () {
        return DoRegister(std::move(keyInfo));
    }));
}

TFuture<void> TCypressKeyWriter::DoRegister(const TKeyInfoPtr& keyInfo)
{
    auto [owner, id] = std::visit([] (const auto& meta) {
        return std::pair{meta.Owner, meta.Id};
    }, keyInfo->Meta());

    YT_LOG_DEBUG("Registering key (Owner: %v, Id %v)", owner, id);

    // We should not register keys belonging to other owners.
    YT_VERIFY(owner == Config_->Owner);

    auto keyNodePath = GetCypressKeyPath(Config_->Path, owner, id);

    auto keyExpirationTime = std::visit([] (const auto& meta) {
        return meta.ExpiresAt;
    }, keyInfo->Meta());

    auto attributes = CreateEphemeralAttributes();
    attributes->Set("expiration_time", keyExpirationTime + Config_->KeyDeletionDelay);

    TCreateNodeOptions options;
    options.Attributes = attributes;
    auto node = Client_->CreateNode(
        keyNodePath,
        EObjectType::Document,
        std::move(options));

    return node.Apply(
        BIND([
                this,
                keyNodePath = std::move(keyNodePath),
                keyInfo,
                this_ = MakeStrong(this)
            ] (NCypressClient::TNodeId /*nodeId*/) {
                return Client_->SetNode(keyNodePath, ConvertToYsonString(std::move(keyInfo)));
            }));
}

////////////////////////////////////////////////////////////////////////////////

TYPath GetCypressKeyPath(const TYPath& prefixPath, const TOwnerId& owner, const TKeyId& key)
{
    return YPathJoin(prefixPath, ToYPathLiteral(owner), ToYPathLiteral(key));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NSignature
