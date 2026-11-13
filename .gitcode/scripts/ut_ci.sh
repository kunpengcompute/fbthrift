echo 'hello'




case "${branch}" in
	master|br_bk26_930|dev_urma_20221104)
        echo "-------- [ignore] branch '${branch}'"
		;;
	dev_20221114)
        bash run_coverage.sh dev_20221114 60
		;;

    *)
        echo "-------- [ERROR !!!] branch '${branch}' not in case --------"
        exit 1
        ;;
esac