#include "query_rewriter.h"

#include <yt/yt/library/query/base/helpers.h>

namespace NYT::NOrm::NQuery {

using namespace NQueryClient::NAst;

////////////////////////////////////////////////////////////////////////////////

TExpressionPtr DummyFunctionRewriter(TFunctionExpression*)
{
    return nullptr;
}

////////////////////////////////////////////////////////////////////////////////

TQueryRewriter::TQueryRewriter(
    TObjectsHolder* holder,
    TReferenceMapping referenceMapping,
    TFunctionRewriter functionRewriter)
    : TRewriter(holder)
    , ReferenceMapping_(std::move(referenceMapping))
    , FunctionRewriter_(std::move(functionRewriter))
{
    YT_VERIFY(ReferenceMapping_);
    YT_VERIFY(FunctionRewriter_);
}

TExpressionPtr TQueryRewriter::Run(const TExpressionPtr& expr)
{
    TExpressionPtr expr_(expr);
    return Visit(expr_);
}

TExpressionPtr TQueryRewriter::OnReference(TReferenceExpressionPtr referenceExpr)
{
    return ReferenceMapping_(referenceExpr->Reference);
}

TExpressionPtr TQueryRewriter::OnFunction(TFunctionExpressionPtr functionExpr)
{
    if (auto* newExpr = FunctionRewriter_(functionExpr)) {
        return newExpr;
    }
    functionExpr->Arguments = std::move(Visit(functionExpr->Arguments));
    return functionExpr;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NOrm::NQuery
